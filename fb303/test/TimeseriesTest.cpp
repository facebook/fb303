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

#include <fb303/Timeseries.h>

#include <gtest/gtest.h>

using namespace std;
using namespace facebook::fb303;
using std::chrono::seconds;

/*
 * Helper function to log timeseries sum data for debugging purposes in the
 * test.
 */
template <typename T>
std::ostream& operator<<(
    std::ostream& os,
    const facebook::fb303::MultiLevelTimeSeries<T>& ts) {
  for (size_t n = 0; n < ts.numLevels(); ++n) {
    if (n != 0) {
      os << "/";
    }
    os << ts.sum(n);
  }
  return os;
}

TEST(MinuteHourTimeSeries, Basic) {
  using IntMHTS = MinuteHourTimeSeries<int>;
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

  IntMHTS::TimePoint cur_time(IntMHTS::Duration(0));

  mhts.addValue(cur_time, 10);
  cur_time += 1s;
  mhts.flush();
  EXPECT_EQ(mhts.getLevel(IntMHTS::MINUTE).elapsed().count(), 1);
  EXPECT_EQ(mhts.getLevel(IntMHTS::HOUR).elapsed().count(), 1);
  EXPECT_EQ(mhts.getLevel(IntMHTS::ALLTIME).elapsed().count(), 1);

  for (int i = 0; i < 299; ++i) {
    mhts.addValue(cur_time, 10);
    cur_time += 1s;
  }
  mhts.flush();

  LOG(INFO) << "after 300 at 10: " << mhts;

  // `elapsed` is calculated as latest - earliest + Duration{1}
  // latest in this case is 299000ms, earliest tracked by the MINUTE
  // timeseries is 240000ms. The timeseries only tracks 59s worth of
  // data because the sliding window just moved into a new (and empty)
  // bucket, and it lost all the data from the tail bucket (1s worth of data).
  // It always adds Duration{1} at the end to capture the inclusive interval.
  // Note that if the timeseires' clock only has second granularity, it would
  // report 60s has elapsed. Having fine granular clock improves the timeseries'
  // precision.
  EXPECT_EQ(mhts.getLevel(IntMHTS::MINUTE).elapsed(), 59'001ms);
  // HOUR and ALLTIME timeseries do not have the time window roll over
  // From their point of view only 299s have passed, and plus Duration{1}
  // to capture the inclusive interval.
  EXPECT_EQ(mhts.getLevel(IntMHTS::HOUR).elapsed(), 299'001ms);
  EXPECT_EQ(mhts.getLevel(IntMHTS::ALLTIME).elapsed(), 299'001ms);

  EXPECT_EQ(mhts.sum(IntMHTS::MINUTE), 600);
  EXPECT_EQ(mhts.sum(IntMHTS::HOUR), 300 * 10);
  EXPECT_EQ(mhts.sum(IntMHTS::ALLTIME), 300 * 10);

  EXPECT_EQ(mhts.avg<int>(IntMHTS::MINUTE), 10);
  EXPECT_EQ(mhts.avg<int>(IntMHTS::HOUR), 10);
  EXPECT_EQ(mhts.avg<int>(IntMHTS::ALLTIME), 10);

  // We default the rate calculation to be per-second
  EXPECT_EQ(mhts.rate<int>(IntMHTS::MINUTE), 10);
  EXPECT_EQ(mhts.rate<int>(IntMHTS::HOUR), 10);
  EXPECT_EQ(mhts.rate<int>(IntMHTS::ALLTIME), 10);

  for (int i = 0; i < 3600 * 3 - 300; ++i) {
    mhts.addValue(cur_time, 10);
    cur_time += 1s;
  }
  mhts.flush();

  LOG(INFO) << "after 3600*3 at 10: " << mhts;

  // A total of 10799s (3600 * 3s - 1s) has passed.
  // Both MINUTE and HOUR timeseries have rolled over. There are
  // 60 buckets in each timeseries. The bucket in MINUTE timesieres
  // stores data for one second; the bucket in the HOUR timeseries
  // store data for one minute. All the first 59 buckets in the HOUR timeseries
  // are full, tracking 59 * 60s (3540s) worth of data. The last bucket
  // is only partially filled with 59s worht of data. Hence it only tracks
  // 3599 seconds worth of data.
  EXPECT_EQ(mhts.getLevel(IntMHTS::MINUTE).elapsed(), 59'001ms);
  EXPECT_EQ(mhts.getLevel(IntMHTS::HOUR).elapsed(), 3'599'001ms);
  EXPECT_EQ(
      mhts.getLevel(IntMHTS::ALLTIME).elapsed(),
      10799001ms /* 3600s * 3 - 1s + 1ms */);

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
    mhts.addValue(cur_time, 100);
    cur_time += 1s;
  }
  mhts.flush();

  LOG(INFO) << "after 3600 at 100: " << mhts;
  EXPECT_EQ(mhts.sum(IntMHTS::MINUTE), 60 * 100);
  EXPECT_EQ(mhts.sum(IntMHTS::HOUR), 3600 * 100);
  EXPECT_EQ(mhts.sum(IntMHTS::ALLTIME), 3600 * 3 * 10 + 3600 * 100);

  EXPECT_EQ(mhts.avg<int>(IntMHTS::MINUTE), 100);
  EXPECT_EQ(mhts.avg<int>(IntMHTS::HOUR), 100);
  EXPECT_EQ(mhts.avg<int>(IntMHTS::ALLTIME), 32);

  // only 59s worth of data is being tracked by the MINUTE timeseries
  // the per-second rate is 6000/(59s + 1ms)
  EXPECT_EQ(mhts.rate<int>(IntMHTS::MINUTE), 101);
  EXPECT_EQ(mhts.rate<int>(IntMHTS::HOUR), 100);
  EXPECT_EQ(mhts.rate<int>(IntMHTS::ALLTIME), 32);

  for (int i = 0; i < 1800; ++i) {
    mhts.addValue(cur_time, 120);
    cur_time += 1s;
  }
  mhts.flush();

  LOG(INFO) << "after 1800 at 120: " << mhts;
  EXPECT_EQ(mhts.sum(IntMHTS::MINUTE), 60 * 120);
  EXPECT_EQ(mhts.sum(IntMHTS::HOUR), 1800 * 100 + 1800 * 120);
  EXPECT_EQ(
      mhts.sum(IntMHTS::ALLTIME), 3600 * 3 * 10 + 3600 * 100 + 1800 * 120);

  for (int i = 0; i < 60; ++i) {
    mhts.addValue(cur_time, 1000);
    cur_time += 1s;
  }
  mhts.flush();

  LOG(INFO) << "after 60 at 1000: " << mhts;
  EXPECT_EQ(mhts.sum(IntMHTS::MINUTE), 60 * 1000);
  EXPECT_EQ(mhts.sum(IntMHTS::HOUR), 1740 * 100 + 1800 * 120 + 60 * 1000);
  EXPECT_EQ(
      mhts.sum(IntMHTS::ALLTIME),
      3600 * 3 * 10 + 3600 * 100 + 1800 * 120 + 60 * 1000);

  // Test non-integral rates
  mhts.addValue(cur_time, 23);
  mhts.flush();
  // since the MINUTE timeseries rolls over, it only trackes 59s + 1ms worth of
  // data
  EXPECT_NEAR(mhts.rate<double>(IntMHTS::MINUTE), 59023 / 59.001, 0.001);

  mhts.clear();
  EXPECT_EQ(mhts.sum(IntMHTS::ALLTIME), 0);
}

TEST(MinuteHourTimeSeries, QueryByInterval) {
  using IntMHTS = MinuteHourTimeSeries<int>;
  IntMHTS mhts;

  IntMHTS::TimePoint curTime;
  for (curTime = IntMHTS::TimePoint(0s); curTime < IntMHTS::TimePoint(7200s);
       curTime++) {
    mhts.addValue(curTime, 1);
  }
  for (curTime = IntMHTS::TimePoint(7200s);
       curTime < IntMHTS::TimePoint(7200s + 3540s);
       curTime++) {
    mhts.addValue(curTime, 10);
  }
  for (curTime = IntMHTS::TimePoint(7200s + 3540s);
       curTime < IntMHTS::TimePoint(7200s + 3600s);
       curTime++) {
    mhts.addValue(curTime, 100);
  }

  // the expectation is that one should always call update() before reading,
  // which flushes all the cached value and moves the clock forward for each
  // BucketedTimeSeries
  mhts.update(curTime - IntMHTS::Duration{1});

  struct TimeInterval {
    IntMHTS::TimePoint start;
    IntMHTS::TimePoint end;
  };
  TimeInterval intervals[12] = {
      {curTime - 60s, curTime},
      {curTime - 3600s, curTime},
      {curTime - 7200s, curTime},
      {curTime - 3600s, curTime - 60s},
      {curTime - 7200s, curTime - 60s},
      {curTime - 7200s, curTime - 3600s},
      {curTime - 50s, curTime - 20s},
      {curTime - 3020s, curTime - 20s},
      {curTime - 7200s, curTime - 20s},
      {curTime - 3000s, curTime - 1000s},
      {curTime - 7200s, curTime - 1000s},
      {curTime - 7200s, curTime - 3600s},
  };

  int expectedSums[12] = {
      6'000'000,
      41'400'000,
      32'400'000,
      35'400'000,
      32'130'000,
      16'200'000,
      30'00'000,
      33'600'000,
      32'310'000,
      20'000'000,
      27'900'000,
      16'200'000,
  };

  int expectedCounts[12] = {
      60'000,
      3'600'000,
      7200'000,
      3'540'000,
      7'140'000,
      3'600'000,
      30'000,
      3'000'000,
      7'180'000,
      2'000'000,
      6'200'000,
      3'600'000,
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
    int expectedRate = expectedSums[i] /
        std::chrono::duration_cast<std::chrono::seconds>(
            interval.end - interval.start)
            .count();
    EXPECT_EQ(expectedRate, r);
  }
}
