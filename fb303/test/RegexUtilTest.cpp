/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <fb303/detail/RegexUtil.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <functional>
#include <memory>
#include <stop_token>
#include <string>
#include <thread>
#include <vector>

#include <fmt/core.h>
#include <folly/Synchronized.h>
#include <folly/container/F14Set.h>
#include <folly/container/RegexMatchCache.h>
#include <folly/lang/Hint.h>
#include <folly/synchronization/Baton.h>
#include <gtest/gtest.h>

using facebook::fb303::detail::cachedAddString;
using facebook::fb303::detail::cachedFindMatches;
using facebook::fb303::detail::cachedFindMatchesSnapshot;
using folly::RegexMatchCache;
using folly::RegexMatchCacheKeyAndView;

namespace {

/// A one-shard stand-in for CallbackValuesMap::CallbackMap: a key-owning,
/// copyable handle in a set beside the match cache. CallbackValuesMap shards
/// 128 ways over a private member, so a per-shard lock hold is not observable
/// through its public API; the SyncMap concept that cachedFindMatchesSnapshot
/// is written against lets a test supply its own single shard instead.
struct TestMap {
  struct Entry {
    std::string const name;

    explicit Entry(std::string value) noexcept : name{std::move(value)} {}
  };

  using SPtr = std::shared_ptr<Entry>;

  struct KeyAccessor {
    std::string const& operator()(SPtr const& ptr) const noexcept {
      return ptr->name;
    }
  };
  static constexpr KeyAccessor fb303_key_accessor{};

  struct Hash {
    size_t operator()(SPtr const& ptr) const noexcept {
      return std::hash<std::string>{}(ptr->name);
    }
  };

  struct EqualTo {
    bool operator()(SPtr const& lhs, SPtr const& rhs) const noexcept {
      return lhs->name == rhs->name;
    }
  };

  folly::F14VectorSet<SPtr, Hash, EqualTo> map;
  mutable RegexMatchCache matches;
};

using SyncTestMap = folly::Synchronized<TestMap>;

constexpr size_t kNumKeys = 2000;
constexpr size_t kNumTokens = 2000;
constexpr size_t kAlternationWidth = 1000;

std::string makeKey(size_t const index) {
  return fmt::format("svc.tok{:05}.metric{:06}.p99", index % kNumTokens, index);
}

/// A wide literal alternation, the shape of a production counter allowlist, so
/// that per-key match cost dominates the scan as it does in production. Keys
/// whose token falls outside the alternation traverse every branch.
std::string makeRegex() {
  std::string out{"^svc\\.(?:"};
  for (size_t i = 0; i < kAlternationWidth; ++i) {
    out += fmt::format("{}tok{:05}", i == 0 ? "" : "|", i);
  }
  out += ")\\.metric[0-9]+\\.p99$";
  return out;
}

size_t expectedMatchCount() {
  size_t count = 0;
  for (size_t i = 0; i < kNumKeys; ++i) {
    count += size_t{i % kNumTokens < kAlternationWidth};
  }
  return count;
}

void populate(SyncTestMap& sync) {
  auto w = sync.wlock();
  for (size_t i = 0; i < kNumKeys; ++i) {
    cachedAddString(*w, std::make_shared<TestMap::Entry>(makeKey(i)));
  }
}

struct BuildObservation {
  std::chrono::nanoseconds scanWall{};
  std::chrono::nanoseconds maxWriterWait{};
  std::vector<std::string> matches;
};

/// Runs one cold build while a second thread repeatedly acquires the map's
/// exclusive lock, and reports the build's total wall time beside the longest
/// single acquisition the writer waited. The writer only reads under the lock:
/// inserting would queue strings mid-build and exercise the residual drain,
/// which is a different property.
template <typename BuildFn>
BuildObservation runBuildWithWriterProbe(
    SyncTestMap& sync,
    std::string const& regex,
    BuildFn build) {
  std::chrono::nanoseconds maxWait{};
  folly::Baton<> probeReady;

  // jthread, not thread: a throwing build must still stop and join the probe,
  // where a joinable std::thread would instead terminate.
  std::jthread probe{[&](std::stop_token const& stop) {
    probeReady.post();
    while (!stop.stop_requested()) {
      auto const before = std::chrono::steady_clock::now();
      {
        auto w = sync.wlock();
        folly::compiler_must_not_elide(w->map.size());
      }
      maxWait = std::max(maxWait, std::chrono::steady_clock::now() - before);
      std::this_thread::yield();
    }
  }};

  probeReady.wait();

  auto const key = RegexMatchCacheKeyAndView{regex};
  BuildObservation observation;
  auto const start = std::chrono::steady_clock::now();
  build(observation.matches, sync, key, RegexMatchCache::clock::now());
  observation.scanWall = std::chrono::steady_clock::now() - start;

  // Explicit, so that maxWait is read after the probe has certainly stopped.
  probe.request_stop();
  probe.join();

  observation.maxWriterWait = maxWait;
  std::sort(observation.matches.begin(), observation.matches.end());
  return observation;
}

double waitFraction(BuildObservation const& observation) {
  return double(observation.maxWriterWait.count()) /
      double(observation.scanWall.count());
}

} // namespace

/// The assertion the off-lock build exists to satisfy: a writer contending for
/// the map during a cold build waits for a small fraction of the scan rather
/// than for the whole of it. The threshold is relative so that machine speed,
/// container throttling and build flavour normalise out.
///
/// The under-lock arm is a positive control. Without it a probe that never
/// observed any hold at all would also satisfy the off-lock bound, and the
/// test would pass while measuring nothing.
TEST(RegexUtilTest, snapshot_build_releases_lock_across_the_scan) {
  auto const regex = makeRegex();

  SyncTestMap underLock;
  populate(underLock);
  auto const blocking = runBuildWithWriterProbe(
      underLock, regex, [](auto& out, auto& map, auto const& key, auto now) {
        cachedFindMatches(out, map, key, now);
      });

  SyncTestMap offLock;
  populate(offLock);
  auto const released = runBuildWithWriterProbe(
      offLock, regex, [](auto& out, auto& map, auto const& key, auto now) {
        cachedFindMatchesSnapshot(out, map, key, now);
      });

  EXPECT_EQ(expectedMatchCount(), released.matches.size());
  EXPECT_EQ(blocking.matches, released.matches);

  EXPECT_GT(waitFraction(blocking), 0.5)
      << "under-lock build did not block the writer, so the probe is not "
         "measuring the lock hold";
  EXPECT_LT(waitFraction(released), 0.25)
      << "off-lock build held the writer for " << waitFraction(released) * 100.0
      << "% of a "
      << std::chrono::duration_cast<std::chrono::milliseconds>(
             released.scanWall)
             .count()
      << " ms scan";
}

/// The blast radius of a match-time throw: the in-flight regex is erased and an
/// unrelated warm regex is left ready, rather than both going through the
/// repair() guard that a drain failure takes.
///
/// CallbackValuesMap cannot express this. Its map and cache are private, and a
/// regex that survived is indistinguishable through getRegexKeys from one that
/// was purged and rebuilt on the next query, so the equivalent test there can
/// only show that the cache still answers.
TEST(RegexUtilTest, snapshot_build_match_time_throw_erases_only_that_regex) {
  SyncTestMap sync;
  {
    auto w = sync.wlock();
    cachedAddString(*w, std::make_shared<TestMap::Entry>(makeKey(0)));
    // A long run of 'a's with a non-matching tail makes (a+)+$ backtrack
    // exponentially, so boost aborts the match with an exception.
    cachedAddString(
        *w, std::make_shared<TestMap::Entry>(std::string(64, 'a') + "!"));
  }

  auto const warm = RegexMatchCacheKeyAndView{"^svc\\..*"};
  auto const throwing = RegexMatchCacheKeyAndView{"(a+)+$"};

  std::vector<std::string> matched;
  cachedFindMatchesSnapshot(matched, sync, warm, RegexMatchCache::clock::now());
  EXPECT_EQ(std::vector<std::string>{makeKey(0)}, matched);

  std::vector<std::string> unused;
  EXPECT_ANY_THROW(cachedFindMatchesSnapshot(
      unused, sync, throwing, RegexMatchCache::clock::now()));

  auto const r = sync.rlock();
  EXPECT_TRUE(r->matches.isReadyToFindMatches(warm));
  EXPECT_FALSE(r->matches.hasRegex(throwing));
}

/// The double-checked early return: a second caller arriving after the cache is
/// warm serves from the shared lock and never reaches the off-lock path.
TEST(RegexUtilTest, snapshot_build_serves_warm_regex_without_rebuilding) {
  auto const regex = makeRegex();
  auto const key = RegexMatchCacheKeyAndView{regex};

  SyncTestMap sync;
  populate(sync);

  std::vector<std::string> cold;
  cachedFindMatchesSnapshot(cold, sync, key, RegexMatchCache::clock::now());

  std::vector<std::string> warm;
  cachedFindMatchesSnapshot(warm, sync, key, RegexMatchCache::clock::now());

  std::sort(cold.begin(), cold.end());
  std::sort(warm.begin(), warm.end());
  EXPECT_EQ(expectedMatchCount(), cold.size());
  EXPECT_EQ(cold, warm);
}
