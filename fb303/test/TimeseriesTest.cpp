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

#include <fb303/Timeseries.h>

#include <gtest/gtest.h>

using namespace std;
using namespace facebook;
using namespace facebook::fb303;
using std::chrono::seconds;

using StatsClock = folly::LegacyStatsClock<std::chrono::seconds>;
using TimePoint = StatsClock::time_point;

/*
 * Helper function to log timeseries sum data for debugging purposes in the
 * test.
 */
template <typename T>
std::ostream& operator<<(
    std::ostream& os,
    const folly::MultiLevelTimeSeries<T>& ts) {
  for (size_t n = 0; n < ts.numLevels(); ++n) {
    if (n != 0) {
      os << "/";
    }
    os << ts.sum(n);
  }
  return os;
}

TEST(MinuteHourTimeSeries, Basic) {
  typedef MinuteHourTimeSeries<int> IntMHTS;
  IntMHTS mhts;

  EXPECT_EQ(mhts.numLevels(), IntMHTS::NUM_LEVELS);
  EXPECT_EQ(mhts.numLevels(), 3);
  mhts.flush();

  LOG(INFO) << "init: " << mhts;
  EXPECT_EQ(mhts.sum(IntMHTS::MINUTE), 0);
  EXPECT_EQ(mhts.sum(IntMHTS::HOUR), 0);
  EXPECT_EQ(mhts.sum(IntMHTS::ALLTIME), 0);

  EXPECT_EQ(mhts.avg<int>(IntMHTS::MINUTE), 0);
  EXPECT_EQ(mhts.avg<int>(IntMHTS::HOUR), 0);
  EXPECT_EQ(mhts.avg<int>(IntMHTS::ALLTIME), 0);

  EXPECT_EQ(mhts.rate<int>(IntMHTS::MINUTE), 0);
  EXPECT_EQ(mhts.rate<int>(IntMHTS::HOUR), 0);
  EXPECT_EQ(mhts.rate<int>(IntMHTS::ALLTIME), 0);

  EXPECT_EQ(mhts.getLevel(IntMHTS::MINUTE).elapsed().count(), 0);
  EXPECT_EQ(mhts.getLevel(IntMHTS::HOUR).elapsed().count(), 0);
  EXPECT_EQ(mhts.getLevel(IntMHTS::ALLTIME).elapsed().count(), 0);

  int cur_time = 0;

  mhts.addValue(cur_time++, 10);
  mhts.flush();
  EXPECT_EQ(mhts.getLevel(IntMHTS::MINUTE).elapsed().count(), 1);
  EXPECT_EQ(mhts.getLevel(IntMHTS::HOUR).elapsed().count(), 1);
  EXPECT_EQ(mhts.getLevel(IntMHTS::ALLTIME).elapsed().count(), 1);

  for (int i = 0; i < 299; ++i) {
    mhts.addValue(cur_time++, 10);
  }
  mhts.flush();

  LOG(INFO) << "after 300 at 10: " << mhts;

  EXPECT_EQ(mhts.getLevel(IntMHTS::MINUTE).elapsed().count(), 60);
  EXPECT_EQ(mhts.getLevel(IntMHTS::HOUR).elapsed().count(), 300);
  EXPECT_EQ(mhts.getLevel(IntMHTS::ALLTIME).elapsed().count(), 300);

  EXPECT_EQ(mhts.sum(IntMHTS::MINUTE), 600);
  EXPECT_EQ(mhts.sum(IntMHTS::HOUR), 300 * 10);
  EXPECT_EQ(mhts.sum(IntMHTS::ALLTIME), 300 * 10);

  EXPECT_EQ(mhts.avg<int>(IntMHTS::MINUTE), 10);
  EXPECT_EQ(mhts.avg<int>(IntMHTS::HOUR), 10);
  EXPECT_EQ(mhts.avg<int>(IntMHTS::ALLTIME), 10);

  EXPECT_EQ(mhts.rate<int>(IntMHTS::MINUTE), 10);
  EXPECT_EQ(mhts.rate<int>(IntMHTS::HOUR), 10);
  EXPECT_EQ(mhts.rate<int>(IntMHTS::ALLTIME), 10);

  for (int i = 0; i < 3600 * 3 - 300; ++i) {
    mhts.addValue(cur_time++, 10);
  }
  mhts.flush();

  LOG(INFO) << "after 3600*3 at 10: " << mhts;

  EXPECT_EQ(mhts.getLevel(IntMHTS::MINUTE).elapsed().count(), 60);
  EXPECT_EQ(mhts.getLevel(IntMHTS::HOUR).elapsed().count(), 3600);
  EXPECT_EQ(mhts.getLevel(IntMHTS::ALLTIME).elapsed().count(), 3600 * 3);

  EXPECT_EQ(mhts.sum(IntMHTS::MINUTE), 600);
  EXPECT_EQ(mhts.sum(IntMHTS::HOUR), 3600 * 10);
  EXPECT_EQ(mhts.sum(IntMHTS::ALLTIME), 3600 * 3 * 10);

  EXPECT_EQ(mhts.avg<int>(IntMHTS::MINUTE), 10);
  EXPECT_EQ(mhts.avg<int>(IntMHTS::HOUR), 10);
  EXPECT_EQ(mhts.avg<int>(IntMHTS::ALLTIME), 10);

  EXPECT_EQ(mhts.rate<int>(IntMHTS::MINUTE), 10);
  EXPECT_EQ(mhts.rate<int>(IntMHTS::HOUR), 10);
  EXPECT_EQ(mhts.rate<int>(IntMHTS::ALLTIME), 10);

  for (int i = 0; i < 3600; ++i) {
    mhts.addValue(cur_time++, 100);
  }
  mhts.flush();

  LOG(INFO) << "after 3600 at 100: " << mhts;
  EXPECT_EQ(mhts.sum(IntMHTS::MINUTE), 60 * 100);
  EXPECT_EQ(mhts.sum(IntMHTS::HOUR), 3600 * 100);
  EXPECT_EQ(mhts.sum(IntMHTS::ALLTIME), 3600 * 3 * 10 + 3600 * 100);

  EXPECT_EQ(mhts.avg<int>(IntMHTS::MINUTE), 100);
  EXPECT_EQ(mhts.avg<int>(IntMHTS::HOUR), 100);
  EXPECT_EQ(mhts.avg<int>(IntMHTS::ALLTIME), 32);

  EXPECT_EQ(mhts.rate<int>(IntMHTS::MINUTE), 100);
  EXPECT_EQ(mhts.rate<int>(IntMHTS::HOUR), 100);
  EXPECT_EQ(mhts.rate<int>(IntMHTS::ALLTIME), 32);

  for (int i = 0; i < 1800; ++i) {
    mhts.addValue(cur_time++, 120);
  }
  mhts.flush();

  LOG(INFO) << "after 1800 at 120: " << mhts;
  EXPECT_EQ(mhts.sum(IntMHTS::MINUTE), 60 * 120);
  EXPECT_EQ(mhts.sum(IntMHTS::HOUR), 1800 * 100 + 1800 * 120);
  EXPECT_EQ(
      mhts.sum(IntMHTS::ALLTIME), 3600 * 3 * 10 + 3600 * 100 + 1800 * 120);

  for (int i = 0; i < 60; ++i) {
    mhts.addValue(cur_time++, 1000);
  }
  mhts.flush();

  LOG(INFO) << "after 60 at 1000: " << mhts;
  EXPECT_EQ(mhts.sum(IntMHTS::MINUTE), 60 * 1000);
  EXPECT_EQ(mhts.sum(IntMHTS::HOUR), 1740 * 100 + 1800 * 120 + 60 * 1000);
  EXPECT_EQ(
      mhts.sum(IntMHTS::ALLTIME),
      3600 * 3 * 10 + 3600 * 100 + 1800 * 120 + 60 * 1000);

  // Test non-integral rates
  mhts.addValue(cur_time++, 23);
  mhts.flush();
  EXPECT_NEAR(mhts.rate<double>(IntMHTS::MINUTE), (double)59023 / 60, 0.001);

  mhts.clear();
  EXPECT_EQ(mhts.sum(IntMHTS::ALLTIME), 0);
}

TEST(MinuteHourTimeSeries, QueryByInterval) {
  typedef MinuteHourTimeSeries<int> IntMHTS;
  IntMHTS mhts;

  int curTime;
  for (curTime = 0; curTime < 7200; curTime++) {
    mhts.addValue(curTime, 1);
  }
  for (curTime = 7200; curTime < 7200 + 3540; curTime++) {
    mhts.addValue(curTime, 10);
  }
  for (curTime = 7200 + 3540; curTime < 7200 + 3600; curTime++) {
    mhts.addValue(curTime, 100);
  }
  mhts.flush();

  struct TimeInterval {
    TimeInterval(int s, int e) : start(seconds(s)), end(seconds(e)) {}

    TimePoint start;
    TimePoint end;
  };
  TimeInterval intervals[12] = {
      {curTime - 60, curTime},
      {curTime - 3600, curTime},
      {curTime - 7200, curTime},
      {curTime - 3600, curTime - 60},
      {curTime - 7200, curTime - 60},
      {curTime - 7200, curTime - 3600},
      {curTime - 50, curTime - 20},
      {curTime - 3020, curTime - 20},
      {curTime - 7200, curTime - 20},
      {curTime - 3000, curTime - 1000},
      {curTime - 7200, curTime - 1000},
      {curTime - 7200, curTime - 3600},
  };

  int expectedSums[12] = {
      6000,
      41400,
      32400,
      35400,
      32130,
      16200,
      3000,
      33600,
      32310,
      20000,
      27900,
      16200,
  };

  int expectedCounts[12] = {
      60,
      3600,
      7200,
      3540,
      7140,
      3600,
      30,
      3000,
      7180,
      2000,
      6200,
      3600,
  };

  for (int i = 0; i < 12; i++) {
    TimeInterval interval = intervals[i];

    int s = mhts.sum(interval.start, interval.end);
    EXPECT_EQ(expectedSums[i], s);

    int c = mhts.count(interval.start, interval.end);
    EXPECT_EQ(expectedCounts[i], c);

    int a = mhts.avg<int>(interval.start, interval.end);
    EXPECT_EQ(expectedCounts[i] ? (expectedSums[i] / expectedCounts[i]) : 0, a);

    int r = mhts.rate<int>(interval.start, interval.end);
    int expectedRate =
        expectedSums[i] / (interval.end - interval.start).count();
    EXPECT_EQ(expectedRate, r);
  }
}
