/*
 * Copyright (c) Facebook, Inc. and its affiliates.
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

#include "common/datastruct/SimpleLRUMap.h"

#include "common/base/StlUtil.h"
#include "common/init/Init.h"
#include "common/time/Clock.h"

#include <folly/Conv.h>
#include <folly/Utility.h>

#include <glog/logging.h>
#include <gtest/gtest.h>

#include <string>
#include <vector>

using namespace std;
using namespace folly;
using namespace facebook;
using namespace facebook::datastruct;
using namespace facebook::stl;

// HELPER FUNCTIONS

template <typename key_type, typename mapped_type>
class evictedCallback {
  key_type key_;
  mapped_type value_;

 public:
  evictedCallback(key_type key, mapped_type value)
      : key_(move(key)), value_(move(value)) {}

  void operator()(pair<const key_type, mapped_type>&& evicted) {
    EXPECT_EQ(evicted.first, key_);
    EXPECT_EQ(evicted.second, value_);
  }
};

typedef SimpleLRUMap<int, string> lru_map;

void noEvictions(lru_map::value_type&&) {
  ADD_FAILURE();
}

evictedCallback<lru_map::key_type, lru_map::mapped_type> checkEvicted(
    lru_map::key_type key,
    lru_map::mapped_type value) {
  return evictedCallback<lru_map::key_type, lru_map::mapped_type>(
      move(key), move(value));
}

template <typename key_type, typename mapped_type>
void checkContents(
    const SimpleLRUMap<key_type, mapped_type>& lru,
    std::vector<std::pair<const key_type, mapped_type>> expected) {
  EXPECT_EQ(lru.empty(), expected.empty());
  EXPECT_EQ(lru.size(), expected.size());

  auto i = lru.cbegin();

  for (const auto& j : expected) {
    EXPECT_NE(i, lru.cend());

    EXPECT_EQ(*i, j);
    ++i;
  }

  EXPECT_EQ(i, lru.cend());

  // lets make sure that the size reported by
  // size() method is the same as the one we
  // see by iterating the list, details: t4225171
  EXPECT_EQ(std::distance(lru.begin(), lru.end()), lru.size());
}

void checkStats(
    const lru_map& lru,
    lru_map::stats_type hits,
    lru_map::stats_type misses) {
  EXPECT_EQ(hits, lru.hits());
  if (hits != lru.hits()) {
    throw std::runtime_error("test failure");
  } // EXPECT_EQ won't exit on failure

  EXPECT_EQ(misses, lru.misses());
  if (misses != lru.misses()) {
    throw std::runtime_error("test failure");
  }

  auto expectedRatio = hits + misses == 0
      ? 0
      : static_cast<lru_map::ratio_type>(hits) /
          (static_cast<lru_map::ratio_type>(hits) +
           static_cast<lru_map::ratio_type>(misses));

  EXPECT_EQ(expectedRatio, lru.hit_ratio());
  if (expectedRatio != lru.hit_ratio()) {
    throw std::runtime_error("test failure");
  }
}

lru_map::mapped_type factory(lru_map::key_type key) {
  return to<string>(key);
}

// TESTS

TEST(SimpleLRUMap, NoSplay) {
  lru_map lru(3);
  EXPECT_TRUE(lru.empty());
  EXPECT_EQ(lru.size(), 0);

  lru.set(0, "0", false);
  checkContents(lru, {{0, "0"}});
  EXPECT_EQ(lru.size(), 1);

  lru.set(1, "1", false);
  checkContents(lru, {{1, "1"}, {0, "0"}});
  EXPECT_EQ(lru.size(), 2);

  lru.set(2, "2", false);
  checkContents(lru, {{2, "2"}, {1, "1"}, {0, "0"}});
  EXPECT_EQ(lru.size(), 3);

  lru.set(1, "1.", false);
  checkContents(lru, {{2, "2"}, {1, "1."}, {0, "0"}});

  lru.set(3, "3", false);
  checkContents(lru, {{3, "3"}, {2, "2"}, {1, "1."}});
  EXPECT_EQ(lru.size(), 3);

  lru.clear();
  checkContents(lru, {});

  EXPECT_TRUE(lru.empty());
  EXPECT_EQ(lru.size(), 0);
  EXPECT_EQ(lru.capacity(), 3);
}

TEST(SimpleLRUMap, SplayWithoutCallback) {
  lru_map lru(3);
  EXPECT_TRUE(lru.empty());
  EXPECT_EQ(lru.size(), 0);

  lru.set(0, "0", true);
  checkContents(lru, {{0, "0"}});
  EXPECT_EQ(lru.size(), 1);

  lru.set(1, "1", true);
  checkContents(lru, {{1, "1"}, {0, "0"}});
  EXPECT_EQ(lru.size(), 2);

  lru.set(2, "2", true);
  checkContents(lru, {{2, "2"}, {1, "1"}, {0, "0"}});
  EXPECT_EQ(lru.size(), 3);

  lru.set(1, "1.", true);
  checkContents(lru, {{1, "1."}, {2, "2"}, {0, "0"}});
  EXPECT_EQ(lru.size(), 3);

  lru.set(3, "3", true);
  checkContents(lru, {{3, "3"}, {1, "1."}, {2, "2"}});
  EXPECT_EQ(lru.size(), 3);

  lru.clear();
  checkContents(lru, {});

  EXPECT_TRUE(lru.empty());
  EXPECT_EQ(lru.size(), 0);
  EXPECT_EQ(lru.capacity(), 3);
}

TEST(SimpleLRUMap, SplayWithCallback) {
  lru_map lru(3);
  EXPECT_TRUE(lru.empty());

  lru.set(0, "0", true, noEvictions);
  checkContents(lru, {{0, "0"}});

  lru.set(1, "1", true, noEvictions);
  checkContents(lru, {{1, "1"}, {0, "0"}});

  lru.set(2, "2", true, noEvictions);
  checkContents(lru, {{2, "2"}, {1, "1"}, {0, "0"}});

  lru.set(1, "1.", true, noEvictions);
  checkContents(lru, {{1, "1."}, {2, "2"}, {0, "0"}});

  lru.set(3, "3", true, checkEvicted(0, "0"));
  checkContents(lru, {{3, "3"}, {1, "1."}, {2, "2"}});

  lru.clear();
  checkContents(lru, {});

  EXPECT_TRUE(lru.empty());
  EXPECT_EQ(lru.capacity(), 3);
}

TEST(SimpleLRUMap, Touch) {
  lru_map lru(2);

  lru.set(0, "0");
  checkContents(lru, {{0, "0"}});
  EXPECT_EQ(lru.touch(0), "0");
  checkContents(lru, {{0, "0"}});
  EXPECT_THROW(lru.touch(1), std::out_of_range);
  checkContents(lru, {{0, "0"}});

  lru.set(1, "1");
  checkContents(lru, {{1, "1"}, {0, "0"}});
  EXPECT_EQ(lru.touch(0), "0");
  checkContents(lru, {{0, "0"}, {1, "1"}});
  EXPECT_EQ(lru.touch(1), "1");
  checkContents(lru, {{1, "1"}, {0, "0"}});
}

TEST(SimpleLRUMap, SplayFind) {
  lru_map lru(2);

  lru.set(0, "0");
  checkContents(lru, {{0, "0"}});
  EXPECT_EQ(*lru.find(0, true), lru_map::value_type(0, "0"));
  checkContents(lru, {{0, "0"}});
  EXPECT_EQ(lru.find(1, true), lru.end());
  checkContents(lru, {{0, "0"}});

  lru.set(1, "1");
  checkContents(lru, {{1, "1"}, {0, "0"}});
  EXPECT_EQ(*lru.find(0, true), lru_map::value_type(0, "0"));
  checkContents(lru, {{0, "0"}, {1, "1"}});
  EXPECT_EQ(*lru.find(1, true), lru_map::value_type(1, "1"));
  checkContents(lru, {{1, "1"}, {0, "0"}});
}

TEST(SimpleLRUMap, NoSplayFind) {
  lru_map lru(2);

  lru.set(0, "0");
  checkContents(lru, {{0, "0"}});
  EXPECT_EQ(*lru.find(0, false), lru_map::value_type(0, "0"));
  checkContents(lru, {{0, "0"}});
  EXPECT_EQ(lru.find(1, false), lru.end());
  checkContents(lru, {{0, "0"}});

  lru.set(1, "1");
  checkContents(lru, {{1, "1"}, {0, "0"}});
  EXPECT_EQ(*lru.find(0, false), lru_map::value_type(0, "0"));
  checkContents(lru, {{1, "1"}, {0, "0"}});
  EXPECT_EQ(*lru.find(1, false), lru_map::value_type(1, "1"));
  checkContents(lru, {{1, "1"}, {0, "0"}});
}

TEST(SimpleLRUMap, FindConst) {
  lru_map lru(2);

  lru.set(0, "0");
  checkContents(lru, {{0, "0"}});
  EXPECT_EQ(*lru.find(0), lru_map::value_type(0, "0"));
  checkContents(lru, {{0, "0"}});
  EXPECT_EQ(lru.find(1), lru.cend());
  checkContents(lru, {{0, "0"}});

  lru.set(1, "1");
  checkContents(lru, {{1, "1"}, {0, "0"}});
  EXPECT_EQ(*lru.find(0), lru_map::value_type(0, "0"));
  checkContents(lru, {{1, "1"}, {0, "0"}});
  EXPECT_EQ(*lru.find(1), lru_map::value_type(1, "1"));
  checkContents(lru, {{1, "1"}, {0, "0"}});
}

TEST(SimpleLRUMap, Peek) {
  lru_map lru(2);

  lru.set(0, "0");
  checkContents(lru, {{0, "0"}});
  EXPECT_EQ(lru.peek(0), "0");
  checkContents(lru, {{0, "0"}});
  EXPECT_THROW(lru.peek(1), std::out_of_range);
  checkContents(lru, {{0, "0"}});

  lru.set(1, "1");
  checkContents(lru, {{1, "1"}, {0, "0"}});
  EXPECT_EQ(lru.peek(0), "0");
  checkContents(lru, {{1, "1"}, {0, "0"}});
  EXPECT_EQ(lru.peek(1), "1");
  checkContents(lru, {{1, "1"}, {0, "0"}});
}

TEST(SimpleLRUMap, OperatorSquareBracket) {
  lru_map lru(2);

  lru.set(0, "0");
  checkContents(lru, {{0, "0"}});
  EXPECT_EQ(lru[0], "0");
  checkContents(lru, {{0, "0"}});
  EXPECT_THROW(lru[1], std::out_of_range);
  checkContents(lru, {{0, "0"}});

  lru.set(1, "1");
  checkContents(lru, {{1, "1"}, {0, "0"}});
  EXPECT_EQ(lru[0], "0");
  checkContents(lru, {{1, "1"}, {0, "0"}});
  EXPECT_EQ(lru[1], "1");
  checkContents(lru, {{1, "1"}, {0, "0"}});
}

TEST(SimpleLRUMap, OperatorSquareBracketConst) {
  lru_map lruMap(2);
  const auto& lru = lruMap;

  lruMap.set(0, "0");
  checkContents(lru, {{0, "0"}});
  EXPECT_EQ(lru[0], "0");
  checkContents(lru, {{0, "0"}});
  EXPECT_THROW(lru[1], std::out_of_range);
  checkContents(lru, {{0, "0"}});

  lruMap.set(1, "1");
  checkContents(lru, {{1, "1"}, {0, "0"}});
  EXPECT_EQ(lru[0], "0");
  checkContents(lru, {{1, "1"}, {0, "0"}});
  EXPECT_EQ(lru[1], "1");
  checkContents(lru, {{1, "1"}, {0, "0"}});
}

TEST(SimpleLRUMap, EraseKey) {
  lru_map lru(5);

  lru.set(4, "4");
  lru.set(3, "3");
  lru.set(2, "2");
  lru.set(1, "1");
  lru.set(0, "0");
  checkContents(lru, {{0, "0"}, {1, "1"}, {2, "2"}, {3, "3"}, {4, "4"}});

  lru.erase(9);
  checkContents(lru, {{0, "0"}, {1, "1"}, {2, "2"}, {3, "3"}, {4, "4"}});

  lru.erase(2);
  checkContents(lru, {{0, "0"}, {1, "1"}, {3, "3"}, {4, "4"}});

  lru.erase(1);
  checkContents(lru, {{0, "0"}, {3, "3"}, {4, "4"}});

  lru.erase(0);
  checkContents(lru, {{3, "3"}, {4, "4"}});

  lru.erase(4);
  checkContents(lru, {{3, "3"}});

  lru.erase(3);
  checkContents(lru, {});
}

TEST(SimpleLRUMap, EraseIterator) {
  lru_map lru(5);

  lru.set(4, "4");
  lru.set(3, "3");
  lru.set(2, "2");
  lru.set(1, "1");
  lru.set(0, "0");
  checkContents(lru, {{0, "0"}, {1, "1"}, {2, "2"}, {3, "3"}, {4, "4"}});

  lru.erase(lru.find(2));
  checkContents(lru, {{0, "0"}, {1, "1"}, {3, "3"}, {4, "4"}});

  lru.erase(lru.find(1));
  checkContents(lru, {{0, "0"}, {3, "3"}, {4, "4"}});

  lru.erase(lru.find(0));
  checkContents(lru, {{3, "3"}, {4, "4"}});

  lru.erase(lru.find(4));
  checkContents(lru, {{3, "3"}});

  lru.erase(lru.find(3));
  checkContents(lru, {});
}

TEST(SimpleLRUMap, ConstComplexity) {
  // lets insert 4M elements into a cache map
  // holding 1M elements and expect this to execute
  // in reasonable time, details: t4225171

  facebook::coarse_stop_watch<std::chrono::seconds> watch;
  lru_map lru(1 << 20);
  for (int i = 0; i < (1 << 22); ++i) {
    // should be pretty fast as 3 letter string will be stored in the
    // string object itself for fb_string
    lru.set(i, "meh");
    if (i % 50 == 0) {
      // this is very conservative, the test runs in 6s in dbg on my box
      // and in 0.7s in opt
      ASSERT_LT(watch.elapsed(), 120_s);
    }
  }

  // just in case check the size too
  EXPECT_EQ(std::distance(lru.begin(), lru.end()), lru.size());
}

TEST(SimpleLRUMap, Stats) {
  lru_map lru(2);

  enum stat_type { hit, miss, none };

  auto testStats = [&](lru_map::stats_type hits,
                       lru_map::stats_type misses,
                       const vector<stat_type>& expected) {
    auto doCheck = [&](stat_type stat) {
      switch (stat) {
        case hit:
          ++hits;
          LOG(INFO) << "hit";
          break;
        case miss:
          ++misses;
          LOG(INFO) << "miss";
          break;
        case none:
          LOG(INFO) << "no change";
          break;
      };
      checkStats(lru, hits, misses);
    };

    EXPECT_EQ(7, expected.size());
    checkStats(lru, hits, misses);

    lru.find(0);
    doCheck(expected[0]);

    lru.set(0, "0");
    doCheck(expected[1]);

    lru.find(0);
    doCheck(expected[2]);

    lru.set(1, "1");
    doCheck(expected[3]);

    lru.find(1);
    doCheck(expected[4]);

    lru.find(0);
    doCheck(expected[5]);

    lru.find(9);
    doCheck(expected[6]);
  };

  testStats(0, 0, {miss, none, hit, none, hit, hit, miss});

  LOG(INFO) << "clearing";
  lru.clear();
  testStats(0, 0, {miss, none, hit, none, hit, hit, miss});

  LOG(INFO) << "clearing (items only)";
  lru.clear(false);
  testStats(3, 2, {miss, none, hit, none, hit, hit, miss});

  LOG(INFO) << "clearing (stats only)";
  lru.clear_stats();
  testStats(0, 0, {hit, none, hit, none, hit, hit, miss});
}

TEST(SimpleLRUMap, TryGetOrCreateDefaultCtor) {
  lru_map lru(3);
  checkStats(lru, 0, 0);
  checkContents(lru, {});

  lru.try_get_or_create(6);
  checkStats(lru, 0, 1);
  checkContents(lru, {{6, ""}});

  lru.try_get_or_create(7);
  checkStats(lru, 0, 2);
  checkContents(lru, {{7, ""}, {6, ""}});

  lru.try_get_or_create(8, false);
  checkStats(lru, 0, 3);
  checkContents(lru, {{8, ""}, {7, ""}, {6, ""}});

  lru.try_get_or_create(6);
  checkStats(lru, 1, 3);
  checkContents(lru, {{6, ""}, {8, ""}, {7, ""}});

  lru.try_get_or_create(7);
  checkStats(lru, 2, 3);
  checkContents(lru, {{7, ""}, {6, ""}, {8, ""}});

  lru.try_get_or_create(8, false);
  checkStats(lru, 3, 3);
  checkContents(lru, {{7, ""}, {6, ""}, {8, ""}});
}

TEST(SimpleLRUMap, TryGetOrCreateFactory) {
  lru_map lru(3);
  checkStats(lru, 0, 0);
  checkContents(lru, {});

  lru.try_get_or_create(6, factory);
  checkStats(lru, 0, 1);
  checkContents(lru, {{6, "6"}});

  lru.try_get_or_create(7, factory);
  checkStats(lru, 0, 2);
  checkContents(lru, {{7, "7"}, {6, "6"}});

  lru.try_get_or_create(8, factory, false);
  checkStats(lru, 0, 3);
  checkContents(lru, {{8, "8"}, {7, "7"}, {6, "6"}});

  lru.try_get_or_create(6, factory);
  checkStats(lru, 1, 3);
  checkContents(lru, {{6, "6"}, {8, "8"}, {7, "7"}});

  lru.try_get_or_create(7, factory);
  checkStats(lru, 2, 3);
  checkContents(lru, {{7, "7"}, {6, "6"}, {8, "8"}});

  lru.try_get_or_create(8, factory, false);
  checkStats(lru, 3, 3);
  checkContents(lru, {{7, "7"}, {6, "6"}, {8, "8"}});
}

TEST(SimpleLRUMap, GetOrCreateDefaultCtor) {
  lru_map lru(3);
  checkStats(lru, 0, 0);
  checkContents(lru, {});

  lru.get_or_create(6);
  checkStats(lru, 0, 1);
  checkContents(lru, {{6, ""}});

  lru.get_or_create(7);
  checkStats(lru, 0, 2);
  checkContents(lru, {{7, ""}, {6, ""}});

  lru.get_or_create(8, false);
  checkStats(lru, 0, 3);
  checkContents(lru, {{8, ""}, {7, ""}, {6, ""}});

  lru.get_or_create(6);
  checkStats(lru, 1, 3);
  checkContents(lru, {{6, ""}, {8, ""}, {7, ""}});

  lru.get_or_create(7);
  checkStats(lru, 2, 3);
  checkContents(lru, {{7, ""}, {6, ""}, {8, ""}});

  lru.get_or_create(8, false);
  checkStats(lru, 3, 3);
  checkContents(lru, {{7, ""}, {6, ""}, {8, ""}});
}

TEST(SimpleLRUMap, GetOrCreateFactory) {
  lru_map lru(3);
  checkStats(lru, 0, 0);
  checkContents(lru, {});

  lru.get_or_create(6, factory);
  checkStats(lru, 0, 1);
  checkContents(lru, {{6, "6"}});

  lru.get_or_create(7, factory);
  checkStats(lru, 0, 2);
  checkContents(lru, {{7, "7"}, {6, "6"}});

  lru.get_or_create(8, factory, false);
  checkStats(lru, 0, 3);
  checkContents(lru, {{8, "8"}, {7, "7"}, {6, "6"}});

  lru.get_or_create(6, factory);
  checkStats(lru, 1, 3);
  checkContents(lru, {{6, "6"}, {8, "8"}, {7, "7"}});

  lru.get_or_create(7, factory);
  checkStats(lru, 2, 3);
  checkContents(lru, {{7, "7"}, {6, "6"}, {8, "8"}});

  lru.get_or_create(8, factory, false);
  checkStats(lru, 3, 3);
  checkContents(lru, {{7, "7"}, {6, "6"}, {8, "8"}});
}

TEST(SimpleLRUMap, Capacity) {
  lru_map lru(1);

  EXPECT_TRUE(lru.empty());
  checkContents(lru, {});
  EXPECT_EQ(1, lru.capacity());

  lru.set(0, "0");
  EXPECT_EQ(1, lru.size());
  checkContents(lru, {{0, "0"}});
  EXPECT_EQ(1, lru.capacity());

  lru.set(1, "1");
  EXPECT_EQ(1, lru.size());
  checkContents(lru, {{1, "1"}});
  EXPECT_EQ(1, lru.capacity());

  lru.capacity(0, checkEvicted(1, "1"));
  EXPECT_TRUE(lru.empty());
  checkContents(lru, {});
  EXPECT_EQ(0, lru.capacity());

  EXPECT_THROW(lru.set(0, "0"), std::length_error);
  EXPECT_TRUE(lru.empty());
  checkContents(lru, {});
  EXPECT_EQ(0, lru.capacity());

  lru.capacity(2, noEvictions);
  EXPECT_TRUE(lru.empty());
  checkContents(lru, {});
  EXPECT_EQ(2, lru.capacity());

  lru.set(0, "0");
  EXPECT_EQ(1, lru.size());
  checkContents(lru, {{0, "0"}});
  EXPECT_EQ(2, lru.capacity());

  lru.set(1, "1");
  EXPECT_EQ(2, lru.size());
  checkContents(lru, {{1, "1"}, {0, "0"}});
  EXPECT_EQ(2, lru.capacity());

  lru.set(2, "2");
  EXPECT_EQ(2, lru.size());
  checkContents(lru, {{2, "2"}, {1, "1"}});
  EXPECT_EQ(2, lru.capacity());
}

TEST(SimpleLRUMap, MapGet) {
  lru_map lru(2);

  string out;
  EXPECT_FALSE(stl::mapGet(lru, 0, &out));
  checkContents(lru, {});

  lru.set(0, "0");
  checkContents(lru, {{0, "0"}});
  EXPECT_TRUE(stl::mapGet(lru, 0, &out));
  EXPECT_EQ("0", out);
  checkContents(lru, {{0, "0"}});

  lru.set(1, "1");
  checkContents(lru, {{1, "1"}, {0, "0"}});
  EXPECT_TRUE(stl::mapGet(lru, 1, &out));
  EXPECT_EQ("1", out);
  checkContents(lru, {{1, "1"}, {0, "0"}});
  EXPECT_TRUE(stl::mapGet(lru, 0, &out));
  EXPECT_EQ("0", out);
  checkContents(lru, {{1, "1"}, {0, "0"}});

  EXPECT_FALSE(stl::mapGet(lru, 9, &out));
  checkContents(lru, {{1, "1"}, {0, "0"}});
}

TEST(SimpleLRUMap, MapGetPtr) {
  lru_map lru(2);

  auto out = stl::mapGetPtr(as_const(lru), 0);
  EXPECT_EQ(nullptr, out);
  checkContents(lru, {});

  lru.set(0, "0");
  checkContents(lru, {{0, "0"}});
  out = stl::mapGetPtr(as_const(lru), 0);
  EXPECT_NE(nullptr, out);
  EXPECT_EQ("0", *out);
  checkContents(lru, {{0, "0"}});

  lru.set(1, "1");
  checkContents(lru, {{1, "1"}, {0, "0"}});
  out = stl::mapGetPtr(as_const(lru), 1);
  EXPECT_NE(nullptr, out);
  EXPECT_EQ("1", *out);
  checkContents(lru, {{1, "1"}, {0, "0"}});
  out = stl::mapGetPtr(as_const(lru), 0);
  EXPECT_NE(nullptr, out);
  EXPECT_EQ("0", *out);
  checkContents(lru, {{1, "1"}, {0, "0"}});

  out = stl::mapGetPtr(as_const(lru), 9);
  EXPECT_EQ(nullptr, out);
  checkContents(lru, {{1, "1"}, {0, "0"}});
}

TEST(SimpleLRUMap, MapGetDefault) {
  lru_map lru(2);

  EXPECT_EQ("x", stl::mapGetDefault(lru, 0, "x"));
  checkContents(lru, {});

  lru.set(0, "0");
  checkContents(lru, {{0, "0"}});
  EXPECT_EQ("0", stl::mapGetDefault(lru, 0, "x"));
  checkContents(lru, {{0, "0"}});

  lru.set(1, "1");
  checkContents(lru, {{1, "1"}, {0, "0"}});
  EXPECT_EQ("1", stl::mapGetDefault(lru, 1, "x"));
  checkContents(lru, {{1, "1"}, {0, "0"}});
  EXPECT_EQ("0", stl::mapGetDefault(lru, 0, "x"));
  checkContents(lru, {{1, "1"}, {0, "0"}});

  EXPECT_EQ("x", stl::mapGetDefault(lru, 9, "x"));
  checkContents(lru, {{1, "1"}, {0, "0"}});
}

TEST(SimpleLRUMap, MapGetRefDefault) {
  lru_map lru(2);

  const std::string x = "x";
  EXPECT_EQ("x", stl::mapGetRefDefault(lru, 0, x));
  checkContents(lru, {});

  lru.set(0, "0");
  checkContents(lru, {{0, "0"}});
  EXPECT_EQ("0", stl::mapGetRefDefault(lru, 0, x));
  checkContents(lru, {{0, "0"}});

  lru.set(1, "1");
  checkContents(lru, {{1, "1"}, {0, "0"}});
  EXPECT_EQ("1", stl::mapGetRefDefault(lru, 1, x));
  checkContents(lru, {{1, "1"}, {0, "0"}});
  EXPECT_EQ("0", stl::mapGetRefDefault(lru, 0, x));
  checkContents(lru, {{1, "1"}, {0, "0"}});

  EXPECT_EQ("x", stl::mapGetRefDefault(lru, 9, x));
  checkContents(lru, {{1, "1"}, {0, "0"}});
}

// DRIVER

int main(int argc, char* argv[]) {
  testing::InitGoogleTest(&argc, argv);
  google::InitGoogleLogging(argv[0]);
  gflags::ParseCommandLineFlags(&argc, &argv, true);

  return RUN_ALL_TESTS();
}
