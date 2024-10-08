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

#pragma once

#include <functional>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

#include <gflags/gflags.h>
#include <glog/logging.h>

#include <folly/Chrono.h>
#include <folly/Optional.h>
#include <folly/Synchronized.h>
#include <folly/container/F14Map.h>
#include <folly/container/RegexMatchCache.h>
#include <folly/synchronization/RelaxedAtomic.h>

#include <fb303/ExportType.h>
#include <fb303/QuantileStat.h>
#include <fb303/detail/RegexUtil.h>

/**
 * Allow services to switch back to the old implementation for RATE. Eventually
 * we need to remove this flag and default to using sum for rate computation.
 */
DECLARE_bool(fb303_qstat_legacy_use_count_for_rate);

namespace facebook::fb303::detail {

/*
 * QuantileStats don't use DynamicCounters because DynamicCounters would not
 * efficiently reuse the TDigest across quantile estimates.
 */
template <typename ClockT>
class BasicQuantileStatMap {
 public:
  using stat_type = BasicQuantileStat<ClockT>;
  using TimePoint = typename ClockT::time_point;

  struct StatDef {
    ExportType type{};
    double quantile{};
  };

  struct SnapshotEntry {
    folly::StringPiece name;
    typename stat_type::Snapshot snapshot;
    std::vector<StatDef> statDefs;
  };

  folly::Optional<int64_t> getValue(folly::StringPiece key) const;
  void getValues(std::map<std::string, int64_t>& out) const;
  void getSelectedValues(
      std::map<std::string, int64_t>& out,
      const std::vector<std::string>& keys) const;

  std::shared_ptr<stat_type> get(folly::StringPiece name) const;
  bool contains(folly::StringPiece name) const;
  void getKeys(std::vector<std::string>& keys) const;

  /* Returns the keys in the map that matches regex pattern */
  void getRegexKeys(std::vector<std::string>& keys, const std::string& regex)
      const {
    const auto key = folly::RegexMatchCache::regex_key_and_view(regex);
    const auto now = folly::RegexMatchCache::clock::now();
    getRegexKeys(keys, key, now);
  }
  void getRegexKeys(
      std::vector<std::string>& keys,
      const folly::RegexMatchCache::regex_key_and_view& regex,
      const folly::RegexMatchCache::time_point now) const;

  size_t getNumKeys() const;

  folly::Optional<SnapshotEntry> getSnapshotEntry(
      folly::StringPiece name,
      TimePoint now = ClockT::now()) const;

  std::shared_ptr<stat_type> registerQuantileStat(
      folly::StringPiece name,
      std::shared_ptr<stat_type> stat,
      std::vector<StatDef> statDefs);

  // BasicQuantileStat buffers added values for a buffer duration.
  // This method can be used to force the buffers to be flushed and
  // rebuild the digests.
  void flushAll() {
    auto countersRLock = counters_.rlock();
    for (auto& p : countersRLock->map) {
      if (p.second.stat != nullptr) {
        p.second.stat->flush();
      }
    }
  }

  void forgetAll() {
    auto countersWLock = counters_.wlock();
    detail::cachedClearStrings(*countersWLock);
    countersWLock->bases.clear();
  }

  void trimRegexCache(folly::RegexMatchCache::time_point expiry) {
    counters_.wlock()->matches.purge(expiry);
  }

 private:
  struct CounterMapEntry {
    std::shared_ptr<stat_type> stat;
    StatDef statDef;
    folly::Optional<std::chrono::seconds> slidingWindowLength;
  };

  struct StatMapEntry {
    std::shared_ptr<stat_type> stat;
    std::vector<StatDef> statDefs;
  };

  // Combining counters map with cache and epoch numbers.  If epochs
  // match, cache is valid.
  template <typename Mapped>
  struct MapWithKeyCache {
    // The key to this map is the fully qualified stat name, e.g. MyStat.p99.60
    folly::F14NodeMap<std::string, Mapped> map;
    // The key to this map is the base of the stat name, e.g. MyStat.
    folly::F14NodeMap<std::string, StatMapEntry> bases;
    folly::RegexMatchCache matches; // requires map to have reference stability
  };
  folly::Synchronized<MapWithKeyCache<CounterMapEntry>> counters_;

  static std::string makeKey(
      folly::StringPiece base,
      const StatDef& statDef,
      const folly::Optional<std::chrono::seconds>& slidingWindowLength);
  static int64_t extractValue(
      const StatDef& statDef,
      const folly::QuantileEstimates& estimate,
      std::chrono::seconds duration);
  static void addValues(
      folly::StringPiece statName,
      const StatDef& statDef,
      const typename stat_type::Estimates& estimate,
      std::chrono::seconds timeSinceCreation,
      std::map<std::string, int64_t>& out);
};

using QuantileStatMap = BasicQuantileStatMap<std::chrono::steady_clock>;

extern template class BasicQuantileStatMap<std::chrono::steady_clock>;

} // namespace facebook::fb303::detail

#include <fb303/detail/QuantileStatMap-inl.h>
