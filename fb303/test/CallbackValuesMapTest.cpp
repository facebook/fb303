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

#include <fb303/CallbackValuesMap.h>

#include <atomic>
#include <chrono>
#include <mutex>
#include <set>
#include <thread>
#include <vector>

#include <boost/bind.hpp>
#include <fmt/core.h>
#include <folly/synchronization/Baton.h>
#include <gtest/gtest.h>

using boost::bind;
using facebook::fb303::CallbackValuesMap;
using std::string;

using TestCallbackValuesMap = CallbackValuesMap<int>;

// simple callback function for testing
static int echo(const int value) {
  return value;
}

static std::set<string> regexKeySet(
    const TestCallbackValuesMap& map,
    const string& regex) {
  std::vector<string> keys;
  map.getRegexKeys(keys, regex);
  return std::set<string>(keys.begin(), keys.end());
}

// ------------------------------------------------------------
// TEST: CallbackValuesMapBasic
// ------------------------------------------------------------

TEST(CallbackValuesMapTest, CallbackValuesMapBasic) {
  TestCallbackValuesMap map;
  const string key1 = "key1";
  const string key2 = "key2";
  // Test empty map
  EXPECT_FALSE(map.contains(key1));
  EXPECT_FALSE(map.contains(key2));
  int value = -1;
  TestCallbackValuesMap::ValuesMap values;
  EXPECT_FALSE(map.getValue(key1, &value));
  EXPECT_FALSE(map.getValue(key2, &value));
  EXPECT_EQ(-1, value);
  map.getValues(&values);
  EXPECT_TRUE(values.empty());

  // Add some keys
  map.registerCallback(key1, bind(echo, 123));
  map.registerCallback(key2, bind(echo, 321));
  EXPECT_TRUE(map.contains(key1));
  EXPECT_TRUE(map.contains(key2));
  EXPECT_TRUE(map.getValue(key1, &value));
  EXPECT_EQ(123, value);
  EXPECT_TRUE(map.getValue(key2, &value));
  EXPECT_EQ(321, value);
  map.getValues(&values);
  EXPECT_EQ(2, values.size());
  EXPECT_EQ(123, values[key1]);
  EXPECT_EQ(321, values[key2]);

  value = -1;
  values.clear();
  // remove some keys
  EXPECT_TRUE(map.unregisterCallback(key1));
  EXPECT_FALSE(map.contains(key1));
  EXPECT_FALSE(map.getValue(key1, &value));
  EXPECT_EQ(-1, value);
  map.getValues(&values);
  EXPECT_EQ(1, values.size());
  EXPECT_EQ(321, values[key2]);
  EXPECT_FALSE(map.unregisterCallback(key1));
}

TEST(CallbackValuesMapTest, PossibleDeadlock) {
  // see t660896 for more details
  std::mutex m;
  m.lock();
  TestCallbackValuesMap callbackMap;
  callbackMap.registerCallback("a", [&m]() {
    m.lock();
    return 0;
  });
  std::thread bg([&callbackMap]() {
    std::map<std::string, int> values;
    callbackMap.getValues(&values);
  }); // this will block for m
  sleep(1);
  callbackMap.registerCallback("b", []() { return 1; });
  m.unlock();
  bg.join();
  SUCCEED();
}

TEST(CallbackValuesMapTest, GetCallback) {
  TestCallbackValuesMap map;
  const string key1 = "key1";
  const string key2 = "key2";
  // Test empty map
  EXPECT_FALSE(map.getCallback(key1));
  EXPECT_FALSE(map.getCallback(key2));
  // Add some keys
  map.registerCallback(key1, bind(echo, 123));
  map.registerCallback(key2, bind(echo, 321));
  auto key1Cb = map.getCallback(key1);
  int val1;
  key1Cb->getValue(&val1);
  EXPECT_EQ(val1, 123);

  auto key2Cb = map.getCallback(key2);
  int val2;
  key2Cb->getValue(&val2);
  EXPECT_EQ(val2, 321);
}

TEST(CallbackValuesMapTest, DoubleDynamicCounterDeadlock) {
  TestCallbackValuesMap callbackMap;
  callbackMap.registerCallback("a", []() { return 42; });
  callbackMap.registerCallback("b", [&callbackMap]() {
    sleep(2);
    int val;
    callbackMap.getValue("a", &val);
    return val;
  });

  std::thread t1([&callbackMap]() {
    int val;
    callbackMap.getValue("b", &val);
  });

  folly::Baton<> baton;
  std::thread t2([&callbackMap, &baton, &t1]() {
    sleep(1);
    callbackMap.unregisterCallback("b");
    t1.join();
    baton.post();
  });
  ASSERT_TRUE(baton.try_wait_for(std::chrono::seconds(10)));
  t2.join();
  SUCCEED();
}

TEST(CallbackValuesMapTest, AggregatesAcrossShards) {
  TestCallbackValuesMap map;
  constexpr size_t kKeys = 1000;
  std::set<string> expected;
  for (size_t i = 0; i < kKeys; ++i) {
    auto key = "key_" + std::to_string(i);
    expected.insert(key);
    map.registerCallback(key, bind(echo, i));
  }

  EXPECT_EQ(kKeys, map.getNumKeys());
  TestCallbackValuesMap::ValuesMap values;
  map.getValues(&values);
  EXPECT_EQ(kKeys, values.size());

  std::vector<string> keys;
  map.getKeys(&keys);
  EXPECT_EQ(expected, std::set<string>(keys.begin(), keys.end()));

  keys.clear();
  map.getRegexKeys(keys, "key_.*");
  EXPECT_EQ(expected, std::set<string>(keys.begin(), keys.end()));

  for (size_t i = 0; i < kKeys; i += 2) {
    EXPECT_TRUE(map.unregisterCallback("key_" + std::to_string(i)));
  }
  EXPECT_EQ(kKeys / 2, map.getNumKeys());

  map.clear();
  EXPECT_EQ(0, map.getNumKeys());
}

// getRegexKeys returns exactly the matching keys, and reflects keys registered
// or unregistered after the regex was first built.
TEST(CallbackValuesMapTest, GetRegexKeysMatchesAndCoalesces) {
  TestCallbackValuesMap map;
  map.registerCallback("foo_1", bind(echo, 1));
  map.registerCallback("foo_2", bind(echo, 2));
  map.registerCallback("bar_1", bind(echo, 3));

  // Cold build over all keys, then a warm read from the coalesced cache.
  EXPECT_EQ(regexKeySet(map, "foo_.*"), (std::set<string>{"foo_1", "foo_2"}));
  EXPECT_EQ(regexKeySet(map, "foo_.*"), (std::set<string>{"foo_1", "foo_2"}));

  EXPECT_EQ(regexKeySet(map, "bar_.*"), (std::set<string>{"bar_1"}));

  map.registerCallback("foo_3", bind(echo, 4));
  EXPECT_EQ(
      regexKeySet(map, "foo_.*"),
      (std::set<string>{"foo_1", "foo_2", "foo_3"}));

  EXPECT_TRUE(map.unregisterCallback("foo_1"));
  EXPECT_EQ(regexKeySet(map, "foo_.*"), (std::set<string>{"foo_2", "foo_3"}));
}

// An invalid pattern is rejected when it is compiled, before the cache is
// mutated, so it must not corrupt the cache for the regex already built or for
// valid queries that follow.
TEST(CallbackValuesMapTest, GetRegexKeysInvalidRegexThrowsAndCacheSurvives) {
  TestCallbackValuesMap map;
  map.registerCallback("foo_1", bind(echo, 1));
  EXPECT_EQ(regexKeySet(map, "foo_.*"), (std::set<string>{"foo_1"}));

  std::vector<string> keys;
  EXPECT_ANY_THROW(map.getRegexKeys(keys, "foo_["));
  EXPECT_EQ(regexKeySet(map, "foo_.*"), (std::set<string>{"foo_1"}));
}

// A pattern that compiles cleanly but throws at match time exercises the
// post-registration failure path: the off-lock build has already registered the
// regex, so it must erase the in-flight regex under the lock and rethrow,
// leaving the cache usable for a following valid query. A long run of 'a's with
// a non-matching tail makes (a+)+$ backtrack exponentially, so boost aborts the
// match with an exception.
TEST(
    CallbackValuesMapTest,
    GetRegexKeysMatchTimeThrowErasesRegexAndCacheSurvives) {
  TestCallbackValuesMap map;
  map.registerCallback(string(64, 'a') + "!", bind(echo, 1));
  map.registerCallback("foo_1", bind(echo, 2));
  EXPECT_EQ(regexKeySet(map, "foo_.*"), (std::set<string>{"foo_1"}));

  std::vector<string> keys;
  EXPECT_ANY_THROW(map.getRegexKeys(keys, "(a+)+$"));
  // a re-query still throws rather than serving a partial match set; that the
  // erase is scoped to the failing regex is asserted directly on the cache in
  // RegexUtilTest.snapshot_build_match_time_throw_erases_only_that_regex
  EXPECT_ANY_THROW(map.getRegexKeys(keys, "(a+)+$"));
  EXPECT_EQ(regexKeySet(map, "foo_.*"), (std::set<string>{"foo_1"}));
}

// Stress the off-lock build: readers build the same regex while writers churn
// unrelated keys. A missing keep-alive would surface as an ASAN use-after-free
// here; churn keys never match, so every read must return the stable set.
TEST(CallbackValuesMapTest, GetRegexKeysConcurrentBuildAndChurn) {
  TestCallbackValuesMap map;
  constexpr int kStable = 2000;
  for (int i = 0; i < kStable; ++i) {
    map.registerCallback(fmt::format("match_{}", i), bind(echo, i));
  }

  std::atomic<bool> stop{false};
  std::atomic<bool> sawNonMatch{false};
  std::atomic<bool> sawWrongCount{false};
  std::vector<std::jthread> threads;
  threads.reserve(6);

  for (int r = 0; r < 4; ++r) {
    threads.emplace_back([&] {
      while (!stop.load(std::memory_order_relaxed)) {
        std::vector<string> keys;
        map.getRegexKeys(keys, "match_.*");
        if (keys.size() != static_cast<size_t>(kStable)) {
          sawWrongCount.store(true, std::memory_order_relaxed);
        }
        for (const auto& k : keys) {
          if (k.rfind("match_", 0) != 0) {
            sawNonMatch.store(true, std::memory_order_relaxed);
          }
        }
      }
    });
  }

  for (int w = 0; w < 2; ++w) {
    threads.emplace_back([&, w] {
      for (int i = 0; !stop.load(std::memory_order_relaxed); ++i) {
        auto name = fmt::format("churn_{}_{}", w, i % 64);
        map.registerCallback(name, bind(echo, i));
        map.unregisterCallback(name);
      }
    });
  }

  /* sleep override */ std::this_thread::sleep_for(
      std::chrono::milliseconds(300));
  stop.store(true, std::memory_order_relaxed);
  for (auto& t : threads) {
    t.join();
  }

  EXPECT_FALSE(sawNonMatch.load());
  EXPECT_FALSE(sawWrongCount.load());
}

// Every added key matches the pattern, so keys registered mid-build land in the
// queue and are drained under the lock by fini rather than being missed.
// Asserts the build converges to exactly the live matching set.
TEST(CallbackValuesMapTest, GetRegexKeysConvergesWithMatchingChurn) {
  TestCallbackValuesMap map;
  constexpr int kStable = 2000;
  constexpr int kAdded = 500;
  for (int i = 0; i < kStable; ++i) {
    map.registerCallback(fmt::format("match_{}", i), bind(echo, i));
  }

  std::atomic<bool> stop{false};
  std::atomic<bool> sawNonMatch{false};
  std::vector<std::jthread> readers;
  readers.reserve(4);
  for (int r = 0; r < 4; ++r) {
    readers.emplace_back([&] {
      while (!stop.load(std::memory_order_relaxed)) {
        std::vector<string> keys;
        map.getRegexKeys(keys, "match_.*");
        for (const auto& k : keys) {
          if (k.rfind("match_", 0) != 0) {
            sawNonMatch.store(true, std::memory_order_relaxed);
          }
        }
      }
    });
  }

  // Add matching keys across the build window so each lands mid-build.
  for (int i = 0; i < kAdded; ++i) {
    map.registerCallback(fmt::format("match_{}", kStable + i), bind(echo, i));
    /* sleep override */ std::this_thread::sleep_for(
        std::chrono::microseconds(100));
  }
  stop.store(true, std::memory_order_relaxed);
  for (auto& t : readers) {
    t.join();
  }

  std::set<string> expected;
  for (int i = 0; i < kStable + kAdded; ++i) {
    expected.insert(fmt::format("match_{}", i));
  }
  EXPECT_EQ(regexKeySet(map, "match_.*"), expected);
  EXPECT_FALSE(sawNonMatch.load());
}

// A build and a cache purge contend on the same lock. The purge here expires at
// the current time, so it does evict the in-flight regex and the build finishes
// through the under-lock fallback in fini; what this asserts is that racing the
// two still converges on the right result. A stuck build would hang the join;
// corruption would show as a wrong result or ASAN. The access-time stamp that
// protects a build from a production-shaped purge (expiry well in the past) is
// covered by RegexMatchCacheTest.init_stamps_accessed_protects_from_purge.
TEST(CallbackValuesMapTest, GetRegexKeysCompletesUnderConcurrentPurge) {
  TestCallbackValuesMap map;
  constexpr int kStable = 2000;
  for (int i = 0; i < kStable; ++i) {
    map.registerCallback(fmt::format("match_{}", i), bind(echo, i));
  }

  std::atomic<bool> stop{false};
  std::atomic<bool> sawWrong{false};
  std::vector<std::jthread> readers;
  readers.reserve(4);
  for (int r = 0; r < 4; ++r) {
    readers.emplace_back([&] {
      while (!stop.load(std::memory_order_relaxed)) {
        std::vector<string> keys;
        map.getRegexKeys(keys, "match_.*");
        if (keys.size() != static_cast<size_t>(kStable)) {
          sawWrong.store(true, std::memory_order_relaxed);
        }
      }
    });
  }

  std::jthread purger([&] {
    while (!stop.load(std::memory_order_relaxed)) {
      map.trimRegexCache(folly::RegexMatchCache::clock::now());
    }
  });

  /* sleep override */ std::this_thread::sleep_for(
      std::chrono::milliseconds(300));
  stop.store(true, std::memory_order_relaxed);
  purger.join();
  for (auto& t : readers) {
    t.join();
  }

  EXPECT_FALSE(sawWrong.load());
  EXPECT_EQ(regexKeySet(map, "match_.*").size(), static_cast<size_t>(kStable));
}
