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

#include <fb303/detail/QuantileStatMap.h>

#include <atomic>
#include <thread>

#include <gtest/gtest.h>

using namespace facebook::fb303;
using namespace facebook::fb303::detail;

struct MockClock {
 public:
  using duration = std::chrono::steady_clock::duration;
  using time_point = std::chrono::steady_clock::time_point;

  static time_point now() {
    return Now;
  }

  static time_point Now;
};

MockClock::time_point MockClock::Now = MockClock::time_point{};

using TestStat = BasicQuantileStat<MockClock>;
using TestStatMap = BasicQuantileStatMap<MockClock>;

const std::vector<std::pair<std::chrono::seconds, size_t>> slidingWindowDefs = {
    {std::chrono::seconds{1}, 60},
    {std::chrono::seconds{10}, 60},
    {std::chrono::seconds{60}, 60}};

const std::vector<double> quantiles = {.95, .99, .999};

class QuantileStatMapTest : public ::testing::Test {
 public:
  QuantileStatMapTest() : stat(std::make_shared<TestStat>(slidingWindowDefs)) {}

  std::shared_ptr<TestStat> stat;
  TestStatMap statMap;

  void SetUp() override {
    MockClock::Now = MockClock::time_point{};

    std::vector<TestStatMap::StatDef> statDefs;

    statDefs.push_back(TestStatMap::StatDef{ExportType::SUM, 0.0});
    statDefs.push_back(TestStatMap::StatDef{ExportType::AVG, 0.0});
    statDefs.push_back(TestStatMap::StatDef{ExportType::RATE, 0.0});
    statDefs.push_back(TestStatMap::StatDef{ExportType::PERCENT, 0.95});
    statDefs.push_back(TestStatMap::StatDef{ExportType::PERCENT, 0.99});
    statDefs.push_back(TestStatMap::StatDef{ExportType::PERCENT, 0.999});

    statMap.registerQuantileStat("StatName", stat, statDefs);
  }
};

TEST_F(QuantileStatMapTest, GetValues) {
  std::map<std::string, int64_t> values;

  statMap.getValues(values);

  EXPECT_EQ(24, values.size());
  EXPECT_EQ(0, values.find("StatName.sum")->second);
  EXPECT_EQ(0, values.find("StatName.sum.60")->second);
  EXPECT_EQ(0, values.find("StatName.sum.600")->second);
  EXPECT_EQ(0, values.find("StatName.sum.3600")->second);
  EXPECT_EQ(0, values.find("StatName.avg")->second);
  EXPECT_EQ(0, values.find("StatName.avg.60")->second);
  EXPECT_EQ(0, values.find("StatName.avg.600")->second);
  EXPECT_EQ(0, values.find("StatName.avg.3600")->second);
  EXPECT_EQ(0, values.find("StatName.rate")->second);
  EXPECT_EQ(0, values.find("StatName.rate.60")->second);
  EXPECT_EQ(0, values.find("StatName.rate.600")->second);
  EXPECT_EQ(0, values.find("StatName.rate.3600")->second);
  EXPECT_EQ(0, values.find("StatName.p95")->second);
  EXPECT_EQ(0, values.find("StatName.p95.60")->second);
  EXPECT_EQ(0, values.find("StatName.p95.600")->second);
  EXPECT_EQ(0, values.find("StatName.p95.3600")->second);
  EXPECT_EQ(0, values.find("StatName.p99")->second);
  EXPECT_EQ(0, values.find("StatName.p99.60")->second);
  EXPECT_EQ(0, values.find("StatName.p99.600")->second);
  EXPECT_EQ(0, values.find("StatName.p99.3600")->second);
  EXPECT_EQ(0, values.find("StatName.p99.9")->second);
  EXPECT_EQ(0, values.find("StatName.p99.9.60")->second);
  EXPECT_EQ(0, values.find("StatName.p99.9.600")->second);
  EXPECT_EQ(0, values.find("StatName.p99.9.3600")->second);

  // Start at time=1 so that we can test the buffering timing properly.
  MockClock::Now += std::chrono::seconds{1};

  stat->addValue(42);
  stat->addValue(42);

  MockClock::Now += std::chrono::seconds{1};

  values.clear();
  statMap.getValues(values);

  // 600 and 3600 counters are still buffered
  EXPECT_EQ(24, values.size());
  EXPECT_EQ(84, values.find("StatName.sum")->second);
  EXPECT_EQ(84, values.find("StatName.sum.60")->second);
  EXPECT_EQ(0, values.find("StatName.sum.600")->second);
  EXPECT_EQ(0, values.find("StatName.sum.3600")->second);
  EXPECT_EQ(42, values.find("StatName.avg")->second);
  EXPECT_EQ(42, values.find("StatName.avg.60")->second);
  EXPECT_EQ(0, values.find("StatName.avg.600")->second);
  EXPECT_EQ(0, values.find("StatName.avg.3600")->second);
  EXPECT_EQ(42, values.find("StatName.rate")->second);
  EXPECT_EQ(42, values.find("StatName.rate.60")->second); // 84 / 2s
  EXPECT_EQ(0, values.find("StatName.rate.600")->second);
  EXPECT_EQ(0, values.find("StatName.rate.3600")->second);
  EXPECT_EQ(42, values.find("StatName.p95")->second);
  EXPECT_EQ(42, values.find("StatName.p95.60")->second);
  EXPECT_EQ(0, values.find("StatName.p95.600")->second);
  EXPECT_EQ(0, values.find("StatName.p95.3600")->second);
  EXPECT_EQ(42, values.find("StatName.p99")->second);
  EXPECT_EQ(42, values.find("StatName.p99.60")->second);
  EXPECT_EQ(0, values.find("StatName.p99.600")->second);
  EXPECT_EQ(0, values.find("StatName.p99.3600")->second);
  EXPECT_EQ(42, values.find("StatName.p99.9")->second);
  EXPECT_EQ(42, values.find("StatName.p99.9.60")->second);
  EXPECT_EQ(0, values.find("StatName.p99.9.600")->second);
  EXPECT_EQ(0, values.find("StatName.p99.9.3600")->second);

  // After 10 seconds, the 600 buffer merges, but the 3600 buffer does not.
  // The rate drops to 0, because 2 / 12 < 1.
  MockClock::Now += std::chrono::seconds{10};

  values.clear();
  statMap.getValues(values);

  EXPECT_EQ(24, values.size());
  EXPECT_EQ(84, values.find("StatName.sum")->second);
  EXPECT_EQ(84, values.find("StatName.sum.60")->second);
  EXPECT_EQ(84, values.find("StatName.sum.600")->second);
  EXPECT_EQ(0, values.find("StatName.sum.3600")->second);
  EXPECT_EQ(42, values.find("StatName.avg")->second);
  EXPECT_EQ(42, values.find("StatName.avg.60")->second);
  EXPECT_EQ(42, values.find("StatName.avg.600")->second);
  EXPECT_EQ(0, values.find("StatName.avg.3600")->second);
  EXPECT_EQ(7, values.find("StatName.rate")->second); // 84 / 12s
  EXPECT_EQ(7, values.find("StatName.rate.60")->second); // 84 / 12s
  EXPECT_EQ(7, values.find("StatName.rate.600")->second); // 84 / 12s
  EXPECT_EQ(0, values.find("StatName.rate.3600")->second);
  EXPECT_EQ(42, values.find("StatName.p95")->second);
  EXPECT_EQ(42, values.find("StatName.p95.60")->second);
  EXPECT_EQ(42, values.find("StatName.p95.600")->second);
  EXPECT_EQ(0, values.find("StatName.p95.3600")->second);
  EXPECT_EQ(42, values.find("StatName.p99")->second);
  EXPECT_EQ(42, values.find("StatName.p99.60")->second);
  EXPECT_EQ(42, values.find("StatName.p99.600")->second);
  EXPECT_EQ(0, values.find("StatName.p99.3600")->second);
  EXPECT_EQ(42, values.find("StatName.p99.9")->second);
  EXPECT_EQ(42, values.find("StatName.p99.9.60")->second);
  EXPECT_EQ(42, values.find("StatName.p99.9.600")->second);
  EXPECT_EQ(0, values.find("StatName.p99.9.3600")->second);
}

TEST_F(QuantileStatMapTest, getValue) {
  auto value = statMap.getValue("StatName.sum");
  EXPECT_EQ(0, *value);

  value = statMap.getValue("StatName.sum.60");
  EXPECT_EQ(0, *value);

  value = statMap.getValue("DoesNotExist");
  EXPECT_FALSE(value.has_value());

  stat->addValue(42);

  MockClock::Now += std::chrono::seconds{1};

  value = statMap.getValue("StatName.sum");
  EXPECT_EQ(42, *value);

  value = statMap.getValue("StatName.sum.60");
  EXPECT_EQ(42, *value);
}

TEST_F(QuantileStatMapTest, getKeys) {
  std::vector<std::string> keys;
  statMap.getKeys(keys);
  EXPECT_EQ(24, keys.size());
}

TEST_F(QuantileStatMapTest, getSelectedValues) {
  std::vector<std::string> selectedKeys = {
      "StatName.sum",
      "StatName.sum.60",
      "StatName.avg.600",
      "StatName.rate.60"};
  MockClock::Now += std::chrono::seconds{1};

  stat->addValue(120);
  stat->addValue(120);
  stat->addValue(120);

  MockClock::Now += std::chrono::seconds{1};

  std::map<std::string, int64_t> values;
  statMap.getSelectedValues(values, selectedKeys);
  EXPECT_EQ(4, values.size());
  EXPECT_EQ(360, values["StatName.sum"]);
  EXPECT_EQ(360, values["StatName.sum.60"]);

  // The average value is still in the 600 buffer.
  EXPECT_EQ(0, values["StatName.avg.600"]);
  EXPECT_EQ(180, values["StatName.rate.60"]); // "360 / 2"
}

TEST_F(QuantileStatMapTest, Overflow) {
  std::map<std::string, int64_t> values;

  statMap.getValues(values);

  EXPECT_EQ(24, values.size());

  // adding a value that overflows and int conversion
  stat->addValue(std::numeric_limits<double>::max());

  // move clock forward to force computations.
  MockClock::Now += std::chrono::seconds{1};

  values.clear();
  statMap.getValues(values);
  EXPECT_EQ(
      std::numeric_limits<int64_t>::max(), values.find("StatName.sum")->second);
  EXPECT_EQ(
      std::numeric_limits<int64_t>::max(),
      values.find("StatName.p95.60")->second);
}

TEST_F(QuantileStatMapTest, getSnapshotEntry) {
  // Start at time=1 so that we can test the buffering timing properly.
  MockClock::Now += std::chrono::seconds{1};

  stat->addValue(42);
  stat->addValue(42);

  auto entry = statMap.getSnapshotEntry("StatName", MockClock::Now).value();
  auto snapshot = entry.snapshot;
  EXPECT_EQ(entry.name, "StatName");
  EXPECT_EQ(entry.statDefs.size(), 6);
  EXPECT_EQ(snapshot.now, MockClock::Now);
  EXPECT_EQ(snapshot.creationTime, MockClock::Now - std::chrono::seconds{1});
  EXPECT_EQ(snapshot.slidingWindowSnapshot.size(), 3);
}

TEST_F(QuantileStatMapTest, ForgetStatsFor) {
  // Forgetting nonexistent name is a no-op.
  statMap.forgetStatsFor("DoesNotExist");
  EXPECT_EQ(statMap.getNumKeys(), 24);
  EXPECT_TRUE(statMap.contains("StatName.sum"));
  EXPECT_NE(statMap.get("StatName"), nullptr);

  // Add data, verify it's present, then forget.
  MockClock::Now += std::chrono::seconds{1};
  stat->addValue(42);
  MockClock::Now += std::chrono::seconds{1};
  EXPECT_EQ(*statMap.getValue("StatName.sum"), 42);

  statMap.forgetStatsFor("StatName");

  // All accessors should reflect removal.
  EXPECT_EQ(statMap.getNumKeys(), 0);
  EXPECT_FALSE(statMap.contains("StatName.sum"));
  EXPECT_FALSE(statMap.getValue("StatName.sum").has_value());
  EXPECT_EQ(statMap.get("StatName"), nullptr);
  EXPECT_FALSE(statMap.getSnapshotEntry("StatName").has_value());
  std::vector<std::string> keys;
  statMap.getKeys(keys);
  EXPECT_TRUE(keys.empty());
  std::map<std::string, int64_t> values;
  statMap.getValues(values);
  EXPECT_TRUE(values.empty());

  // Second call is idempotent.
  statMap.forgetStatsFor("StatName");
  EXPECT_EQ(statMap.getNumKeys(), 0);
}

TEST_F(QuantileStatMapTest, ForgetAndReRegister) {
  // Register a second stat.
  auto stat2 = std::make_shared<TestStat>(slidingWindowDefs);
  std::vector<TestStatMap::StatDef> statDefs2;
  statDefs2.push_back(TestStatMap::StatDef{ExportType::SUM, 0.0});
  statDefs2.push_back(TestStatMap::StatDef{ExportType::AVG, 0.0});
  statMap.registerQuantileStat("OtherStat", stat2, statDefs2);
  EXPECT_EQ(statMap.getNumKeys(), 32); // 24 + 8

  // Forget one stat; the other survives.
  statMap.forgetStatsFor("StatName");
  EXPECT_EQ(statMap.getNumKeys(), 8);
  EXPECT_FALSE(statMap.contains("StatName.sum"));
  EXPECT_TRUE(statMap.contains("OtherStat.sum"));
  EXPECT_TRUE(statMap.contains("OtherStat.avg.60"));
  EXPECT_EQ(statMap.get("StatName"), nullptr);
  EXPECT_EQ(statMap.get("OtherStat"), stat2);

  // Re-register the forgotten stat with different defs.
  std::vector<TestStatMap::StatDef> newDefs;
  newDefs.push_back(TestStatMap::StatDef{ExportType::SUM, 0.0});
  newDefs.push_back(TestStatMap::StatDef{ExportType::COUNT, 0.0});
  auto newStat = std::make_shared<TestStat>(slidingWindowDefs);
  statMap.registerQuantileStat("StatName", newStat, newDefs);
  EXPECT_EQ(statMap.getNumKeys(), 16); // 8 + 8
  EXPECT_TRUE(statMap.contains("StatName.sum"));
  EXPECT_TRUE(statMap.contains("StatName.count"));
  EXPECT_EQ(statMap.get("StatName"), newStat);

  // forgetAll clears everything.
  statMap.forgetAll();
  EXPECT_EQ(statMap.getNumKeys(), 0);
}

// Stress test: one thread continuously registers and forgets stats,
// another reads values, and a third queries keys.
TEST(QuantileStatMapConcurrency, ConcurrentForgetAndRegister) {
  TestStatMap statMap;

  std::atomic<uint64_t> registerIterations{0};
  std::atomic<uint64_t> forgetIterations{0};
  std::atomic<uint64_t> queryIterations{0};

  auto end = std::chrono::steady_clock::now() + std::chrono::seconds(2);
  std::atomic<bool> done{false};

  std::thread registerThread([&] {
    while (std::chrono::steady_clock::now() < end) {
      for (int n = 0; n < 100; ++n) {
        auto stat = std::make_shared<TestStat>(slidingWindowDefs);
        std::vector<TestStatMap::StatDef> statDefs;
        statDefs.push_back(TestStatMap::StatDef{ExportType::SUM, 0.0});
        statDefs.push_back(TestStatMap::StatDef{ExportType::PERCENT, 0.99});
        statMap.registerQuantileStat("ConcurrentStat", stat, statDefs);
        ++registerIterations;
      }
    }
    done = true;
  });

  std::thread forgetThread([&] {
    while (!done) {
      statMap.forgetStatsFor("ConcurrentStat");
      ++forgetIterations;
    }
  });

  std::thread queryThread([&] {
    while (!done) {
      std::map<std::string, int64_t> values;
      statMap.getValues(values);
      statMap.getValue("ConcurrentStat.sum");
      std::vector<std::string> keys;
      statMap.getKeys(keys);
      ++queryIterations;
    }
  });

  registerThread.join();
  forgetThread.join();
  queryThread.join();

  EXPECT_GT(registerIterations.load(), 1000);
  EXPECT_GT(forgetIterations.load(), 1000);
  EXPECT_GT(queryIterations.load(), 100);
}

// Stress test: multiple stats being forgotten concurrently.
TEST(QuantileStatMapConcurrency, ConcurrentForgetMultipleStats) {
  TestStatMap statMap;

  constexpr int kNumStats = 10;

  // Register multiple stats.
  std::vector<std::shared_ptr<TestStat>> stats;
  for (int i = 0; i < kNumStats; ++i) {
    auto stat = std::make_shared<TestStat>(slidingWindowDefs);
    stats.push_back(stat);
    std::vector<TestStatMap::StatDef> statDefs;
    statDefs.push_back(TestStatMap::StatDef{ExportType::SUM, 0.0});
    statDefs.push_back(TestStatMap::StatDef{ExportType::PERCENT, 0.95});
    statMap.registerQuantileStat("Stat" + std::to_string(i), stat, statDefs);
  }

  // 2 stat defs x 4 windows x 10 stats = 80 keys
  EXPECT_EQ(statMap.getNumKeys(), 80);

  // Forget all stats concurrently from different threads.
  std::vector<std::thread> threads;
  for (int i = 0; i < kNumStats; ++i) {
    threads.emplace_back(
        [&statMap, i] { statMap.forgetStatsFor("Stat" + std::to_string(i)); });
  }
  for (auto& t : threads) {
    t.join();
  }

  EXPECT_EQ(statMap.getNumKeys(), 0);

  std::vector<std::string> keys;
  statMap.getKeys(keys);
  EXPECT_TRUE(keys.empty());
}
