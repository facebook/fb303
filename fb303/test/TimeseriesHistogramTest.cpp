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

#include <fb303/TimeseriesHistogram.h>

#include <gtest/gtest.h>

using namespace std;
using namespace facebook;
using namespace facebook::fb303;

using StatsClock = folly::LegacyStatsClock<std::chrono::seconds>;
using TimePoint = StatsClock::time_point;

template class facebook::fb303::TimeseriesHistogram<int>;

namespace {
TimePoint mkTimePoint(int value) {
  return TimePoint(StatsClock::duration(value));
}
} // namespace

TEST(TimeseriesHistogram, Clear) {
  typedef MinuteTenMinuteHourTimeSeries<int> TS;

  {
    TimeseriesHistogram<int> hist(10, 0, 100);

    for (int now = 0; now < 3600; now++) {
      for (int i = 0; i < 100; i++) {
        hist.addValue(now, i, 2); // adds each item 2 times
      }
    }

    // check clearing
    hist.clear();

    for (size_t b = 0; b < hist.getNumBuckets(); ++b) {
      EXPECT_EQ(0, hist.getBucket(b).count(TS::MINUTE));
      EXPECT_EQ(0, hist.getBucket(b).count(TS::TEN_MINUTE));
      EXPECT_EQ(0, hist.getBucket(b).count(TS::HOUR));
      EXPECT_EQ(0, hist.getBucket(b).count(TS::ALLTIME));
    }

    for (int pct = 0; pct <= 100; pct++) {
      EXPECT_EQ(0, hist.getPercentileBucketMin(pct, TS::MINUTE));
      EXPECT_EQ(0, hist.getPercentileBucketMin(pct, TS::TEN_MINUTE));
      EXPECT_EQ(0, hist.getPercentileBucketMin(pct, TS::HOUR));
      EXPECT_EQ(0, hist.getPercentileBucketMin(pct, TS::ALLTIME));

      EXPECT_EQ(0, hist.getPercentileEstimate(pct, TS::MINUTE));
      EXPECT_EQ(0, hist.getPercentileEstimate(pct, TS::TEN_MINUTE));
      EXPECT_EQ(0, hist.getPercentileEstimate(pct, TS::HOUR));
      EXPECT_EQ(0, hist.getPercentileEstimate(pct, TS::ALLTIME));
    }
  }
}

TEST(TimeseriesHistogram, Basic) {
  typedef MinuteTenMinuteHourTimeSeries<int> TS;

  {
    TimeseriesHistogram<int> hist(10, 0, 100);

    for (int now = 0; now < 3600; now++) {
      for (int i = 0; i < 100; i++) {
        hist.addValue(now, i);
      }
    }

    hist.update(3599);
    for (int pct = 1; pct <= 100; pct++) {
      int expected = (pct - 1) / 10 * 10;
      EXPECT_EQ(expected, hist.getPercentileBucketMin(pct, TS::MINUTE));
      EXPECT_EQ(expected, hist.getPercentileBucketMin(pct, TS::TEN_MINUTE));
      EXPECT_EQ(expected, hist.getPercentileBucketMin(pct, TS::HOUR));
      EXPECT_EQ(expected, hist.getPercentileBucketMin(pct, TS::ALLTIME));
    }

    for (size_t b = 1; (b + 1) < hist.getNumBuckets(); ++b) {
      EXPECT_EQ(600, hist.getBucket(b).count(TS::MINUTE));
      EXPECT_EQ(6000, hist.getBucket(b).count(TS::TEN_MINUTE));
      EXPECT_EQ(36000, hist.getBucket(b).count(TS::HOUR));
      EXPECT_EQ(36000, hist.getBucket(b).count(TS::ALLTIME));
    }
    EXPECT_EQ(0, hist.getBucket(0).count(TS::MINUTE));
    EXPECT_EQ(0, hist.getBucket(hist.getNumBuckets() - 1).count(TS::MINUTE));
  }

  // -----------------

  {
    TimeseriesHistogram<int> hist(10, 0, 100);

    for (int now = 0; now < 3600; now++) {
      for (int i = 0; i < 100; i++) {
        hist.addValue(now, i, 2); // adds each item 2 times
      }
    }

    hist.update(3599);
    for (int pct = 1; pct <= 100; pct++) {
      int expected = (pct - 1) / 10 * 10;
      EXPECT_EQ(expected, hist.getPercentileBucketMin(pct, TS::MINUTE));
      EXPECT_EQ(expected, hist.getPercentileBucketMin(pct, TS::TEN_MINUTE));
      EXPECT_EQ(expected, hist.getPercentileBucketMin(pct, TS::HOUR));
      EXPECT_EQ(expected, hist.getPercentileBucketMin(pct, TS::ALLTIME));
    }

    for (size_t b = 1; (b + 1) < hist.getNumBuckets(); ++b) {
      EXPECT_EQ(600 * 2, hist.getBucket(b).count(TS::MINUTE));
      EXPECT_EQ(6000 * 2, hist.getBucket(b).count(TS::TEN_MINUTE));
      EXPECT_EQ(36000 * 2, hist.getBucket(b).count(TS::HOUR));
      EXPECT_EQ(36000 * 2, hist.getBucket(b).count(TS::ALLTIME));
    }
    EXPECT_EQ(0, hist.getBucket(0).count(TS::MINUTE));
    EXPECT_EQ(0, hist.getBucket(hist.getNumBuckets() - 1).count(TS::MINUTE));
  }

  // -----------------

  {
    TimeseriesHistogram<int> hist(10, 0, 100);

    for (int now = 0; now < 3600; now++) {
      for (int i = 0; i < 50; i++) {
        hist.addValue(now, i * 2, 2); // adds each item 2 times
      }
    }

    hist.update(3599);
    for (int pct = 1; pct <= 100; pct++) {
      int expected = (pct - 1) / 10 * 10;
      EXPECT_EQ(expected, hist.getPercentileBucketMin(pct, TS::MINUTE));
      EXPECT_EQ(expected, hist.getPercentileBucketMin(pct, TS::TEN_MINUTE));
      EXPECT_EQ(expected, hist.getPercentileBucketMin(pct, TS::HOUR));
      EXPECT_EQ(expected, hist.getPercentileBucketMin(pct, TS::ALLTIME));
    }

    EXPECT_EQ(0, hist.getBucket(0).count(TS::MINUTE));
    EXPECT_EQ(0, hist.getBucket(0).count(TS::TEN_MINUTE));
    EXPECT_EQ(0, hist.getBucket(0).count(TS::HOUR));
    EXPECT_EQ(0, hist.getBucket(0).count(TS::ALLTIME));
    EXPECT_EQ(0, hist.getBucket(hist.getNumBuckets() - 1).count(TS::MINUTE));
    EXPECT_EQ(
        0, hist.getBucket(hist.getNumBuckets() - 1).count(TS::TEN_MINUTE));
    EXPECT_EQ(0, hist.getBucket(hist.getNumBuckets() - 1).count(TS::HOUR));
    EXPECT_EQ(0, hist.getBucket(hist.getNumBuckets() - 1).count(TS::ALLTIME));

    for (size_t b = 1; (b + 1) < hist.getNumBuckets(); ++b) {
      EXPECT_EQ(600, hist.getBucket(b).count(TS::MINUTE));
      EXPECT_EQ(6000, hist.getBucket(b).count(TS::TEN_MINUTE));
      EXPECT_EQ(36000, hist.getBucket(b).count(TS::HOUR));
      EXPECT_EQ(36000, hist.getBucket(b).count(TS::ALLTIME));
    }

    for (int i = 0; i < 100; ++i) {
      hist.addValue(3599, 200 + i);
    }
    hist.update(3599);
    EXPECT_EQ(100, hist.getBucket(hist.getNumBuckets() - 1).count(TS::ALLTIME));
  }
}

TEST(TimeseriesHistogram, QueryByInterval) {
  TimeseriesHistogram<int> mhts(8, 8, 120, MinuteHourTimeSeries<int>());

  mhts.update(0);

  int curTime;
  for (curTime = 0; curTime < 7200; curTime++) {
    mhts.addValue(mkTimePoint(curTime), 1);
  }
  for (curTime = 7200; curTime < 7200 + 3540; curTime++) {
    mhts.addValue(mkTimePoint(curTime), 10);
  }
  for (curTime = 7200 + 3540; curTime < 7200 + 3600; curTime++) {
    mhts.addValue(mkTimePoint(curTime), 100);
  }

  mhts.update(7200 + 3600 - 1);

  struct TimeInterval {
    TimeInterval(int s, int e) : start(mkTimePoint(s)), end(mkTimePoint(e)) {}

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
      32129,
      16200,
      3000,
      33600,
      32308,
      20000,
      27899,
      16200,
  };

  int expectedCounts[12] = {
      60,
      3600,
      7200,
      3540,
      7139,
      3600,
      30,
      3000,
      7178,
      2000,
      6199,
      3600,
  };

  // The first 7200 values added all fell below the histogram minimum,
  // and went into the bucket that tracks all of the too-small values.
  // This bucket reports a minimum value of the smallest possible integer.
  int belowMinBucket = std::numeric_limits<int>::min();

  int expectedValues[12][3] = {
      {96, 96, 96},
      {8, 8, 96},
      {belowMinBucket, belowMinBucket, 8}, // alltime
      {8, 8, 8},
      {belowMinBucket, belowMinBucket, 8}, // alltime
      {belowMinBucket, belowMinBucket, 8}, // alltime
      {96, 96, 96},
      {8, 8, 96},
      {belowMinBucket, belowMinBucket, 8}, // alltime
      {8, 8, 8},
      {belowMinBucket, belowMinBucket, 8}, // alltime
      {belowMinBucket, belowMinBucket, 8}, // alltime
  };

  for (int i = 0; i < 12; i++) {
    const auto& itv = intervals[i];
    int s = mhts.sum(itv.start, itv.end);
    EXPECT_EQ(expectedSums[i], s);

    int c = mhts.count(itv.start, itv.end);
    EXPECT_EQ(expectedCounts[i], c);
  }

  // 3 levels
  for (int i = 1; i <= 100; i++) {
    EXPECT_EQ(96, mhts.getPercentileBucketMin(i, 0));
    EXPECT_EQ(
        96,
        mhts.getPercentileBucketMin(
            i, mkTimePoint(curTime - 60), mkTimePoint(curTime)));
    EXPECT_EQ(
        8,
        mhts.getPercentileBucketMin(
            i, mkTimePoint(curTime - 3540), mkTimePoint(curTime - 60)));
  }

  EXPECT_EQ(8, mhts.getPercentileBucketMin(1, 1));
  EXPECT_EQ(8, mhts.getPercentileBucketMin(98, 1));
  EXPECT_EQ(96, mhts.getPercentileBucketMin(99, 1));
  EXPECT_EQ(96, mhts.getPercentileBucketMin(100, 1));

  EXPECT_EQ(belowMinBucket, mhts.getPercentileBucketMin(1, 2));
  EXPECT_EQ(belowMinBucket, mhts.getPercentileBucketMin(66, 2));
  EXPECT_EQ(8, mhts.getPercentileBucketMin(67, 2));
  EXPECT_EQ(8, mhts.getPercentileBucketMin(99, 2));
  EXPECT_EQ(96, mhts.getPercentileBucketMin(100, 2));

  // 0 is currently the value for bucket 0 (below min)
  for (int i = 0; i < 12; i++) {
    const auto& itv = intervals[i];
    int v = mhts.getPercentileBucketMin(1, itv.start, itv.end);
    EXPECT_EQ(expectedValues[i][0], v);

    v = mhts.getPercentileBucketMin(50, itv.start, itv.end);
    EXPECT_EQ(expectedValues[i][1], v);

    v = mhts.getPercentileBucketMin(99, itv.start, itv.end);
    EXPECT_EQ(expectedValues[i][2], v);
  }

  for (int i = 0; i < 12; i++) {
    const auto& itv = intervals[i];
    // Some of the older intervals that fall in the alltime bucket
    // are off by 1 or 2 in their estimated counts.
    size_t tolerance = 0;
    if (itv.start <= mkTimePoint(curTime - 7200)) {
      tolerance = 2;
    } else if (itv.start <= mkTimePoint(curTime - 3000)) {
      tolerance = 1;
    }
    size_t actualCount = (itv.end - itv.start).count();
    size_t estimatedCount = mhts.count(itv.start, itv.end);
    EXPECT_GE(actualCount, estimatedCount);
    EXPECT_LE(actualCount - tolerance, estimatedCount);
  }
}
