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

#include <fb303/ExportedStatMapImpl.h>

#include <fb303/DynamicCounters.h>
#include <gtest/gtest.h>
#include <atomic>
#include <thread>

#include <time.h>
using namespace std;
using namespace facebook;
using namespace facebook::fb303;

void testExportedStatMapImpl(bool useStatPtr) {
  DynamicCounters dc;
  ExportedStatMapImpl statMap(&dc);

  int64_t now = ::time(nullptr);
  if (useStatPtr) {
    ExportedStatMapImpl::StatPtr item = statMap.getStatPtr("test_value");
    statMap.addValue(item, now, 10);
  } else {
    statMap.addValue("test_value", now, 10);
  }

  int64_t tmp;
  EXPECT_FALSE(dc.getCounter("FAKE_test_value.avg.60", &tmp));

  map<string, int64_t> res;
  dc.getCounters(&res);
  FOR_EACH (it, res) {
    LOG(INFO) << "res[\"" << it->first << "\"] = " << it->second;
  }
  EXPECT_EQ(res.size(), 4);
  EXPECT_EQ(res["test_value.avg.60"], 10);

  EXPECT_TRUE(dc.getCounter("test_value.avg.60", &tmp));
  EXPECT_EQ(tmp, 10);
  // now remove the stat
  statMap.unExportStatAll("test_value");
  res.clear();
  dc.getCounters(&res);
  EXPECT_TRUE(res.empty());
}

TEST(ExportedStatMapImpl, ExportedBasics) {
  testExportedStatMapImpl(false);
}

TEST(ExportedStatMapImpl, ExportedLockAndUpdate) {
  testExportedStatMapImpl(true);
}

// Destroy a timeseries stat without first unexporting its exported counters
TEST(ExportedStatMapImpl, ForgetWithoutUnexport) {
  DynamicCounters dc;
  ExportedStatMapImpl statMap(&dc);

  const string statName = "mystat";
  statMap.addValue(statName, 0, 10);
  statMap.addValue(statName, 0, 20);
  statMap.forgetStatsFor(statName);

  int64_t result;
  string queryName = statName + ".avg";
  dc.getCounter(queryName, &result);
  EXPECT_EQ(result, 15);
}

// Have one thread continuously call forgetStatsFor(),
// while another thread is continuously updating (and re-exporting) the stat,
// and a third thread is querying the average value.
TEST(ExportedStatMapImpl, ConcurrentForget) {
  DynamicCounters dc;
  ExportedStatMapImpl statMap(&dc);

  const string statName = "mystat";

  uint64_t updateIterations = 0;
  uint64_t forgetIterations = 0;
  uint64_t queryIterations = 0;

  // Run for 2 seconds
  auto end = std::chrono::steady_clock::now() + std::chrono::seconds(2);
  std::atomic<bool> done(false);
  std::thread updateThread([&] {
    while (std::chrono::steady_clock::now() < end) {
      for (int n = 0; n < 100; ++n) {
        statMap.addValue(statName, 0, 1);
        ++updateIterations;
      }
    }
    done = true;
  });

  std::thread forgetThread([&] {
    while (!done) {
      statMap.forgetStatsFor(statName);
      ++forgetIterations;
    }
  });

  std::thread queryThread([&] {
    string queryName = statName + ".avg";
    while (!done) {
      int64_t result;
      dc.getCounter(queryName, &result);
      ++queryIterations;
    }
  });

  updateThread.join();
  forgetThread.join();
  queryThread.join();

  // Just for sanity, make sure each thread ran through some
  // minimal number of iterations.
  EXPECT_GT(updateIterations, 1000);
  EXPECT_GT(forgetIterations, 1000);
  EXPECT_GT(queryIterations, 1000);
}

void exportStatThread(
    ExportedStatMapImpl* statsMap,
    const std::string& counterName,
    uint32_t numIters,
    uint64_t incrAmount) {
  for (uint32_t n = 0; n < numIters; ++n) {
    statsMap->exportStat(counterName, fb303::SUM);
    statsMap->addValue(counterName, ::time(nullptr), incrAmount);
    sched_yield();
  }
}

// Test calling exportStat() simultaneously from multiple threads,
// while the stat is also being updated.
TEST(ExportedStatMapImpl, MultithreadedExport) {
  DynamicCounters counters;
  ExportedStatMapImpl statsMap(&counters);

  std::string counterName = "foo";
  uint32_t numIters = 1000;
  uint32_t numThreads = 4;
  uint64_t incrAmount = 8;

  std::vector<std::thread> threads;
  for (uint32_t n = 0; n < numThreads; ++n) {
    threads.emplace_back(
        exportStatThread, &statsMap, counterName, numIters, incrAmount);
  }
  for (auto& thread : threads) {
    thread.join();
  }

  auto lockedObj = statsMap.getLockedStatPtr(counterName);
  EXPECT_EQ(numIters * numThreads * incrAmount, lockedObj->sum(0));
}

TEST(LockableStat, Swap) {
  using LockableStat = ExportedStatMapImpl::LockableStat;
  DynamicCounters dc;
  ExportedStatMapImpl statMap(&dc);

  LockableStat statA = statMap.getLockableStat("testA");
  {
    auto guard = statA.lock();
    statA.addValueLocked(guard, ::time(nullptr), 10);
    statA.flushLocked(guard);
  }
  LockableStat statB = statMap.getLockableStat("testB");

  {
    auto guard = statA.lock();
    EXPECT_EQ(guard->sum(0), 10);
    guard = statB.lock();
    EXPECT_EQ(guard->sum(0), 0);
  }

  statA.swap(statB);

  // the stats are reversed now
  // so acquire the locks in the B/A order
  // to avoid a TSAN lock-order-inversion message
  auto guard = statB.lock();
  EXPECT_EQ(guard->sum(0), 10);
  guard = statA.lock();
  EXPECT_EQ(guard->sum(0), 0);
}

TEST(LockableStat, AddValue) {
  using LockableStat = ExportedStatMapImpl::LockableStat;
  DynamicCounters dc;
  ExportedStatMapImpl statMap(&dc);

  time_t now = ::time(nullptr);
  const string name = "test_value";
  LockableStat stat = statMap.getLockableStat(name);
  stat.addValue(now, 10);

  int64_t result;
  string queryname = name + ".avg";
  dc.getCounter(queryname, &result);
  EXPECT_EQ(result, 10);
  {
    auto guard = stat.lock();
    stat.addValueLocked(guard, now, 20);
  }
  dc.getCounter(queryname, &result);
  EXPECT_EQ(result, 15);
}
