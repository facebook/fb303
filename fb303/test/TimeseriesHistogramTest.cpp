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

#include <fb303/TimeseriesHistogram.h>
#include "common/base/Random.h"

#include <gtest/gtest.h>

using namespace std;
using namespace facebook;
using namespace facebook::fb303;
using std::chrono::seconds;

using StatsClock = folly::LegacyStatsClock<std::chrono::seconds>;
using TimePoint = StatsClock::time_point;

template class facebook::fb303::TimeseriesHistogram<int>;

namespace {
TimePoint mkTimePoint(int value) {
  return TimePoint(StatsClock::duration(value));
}
} // namespace

TEST(TimeseriesHistogram, Percentile) {
  typedef MinuteTenMinuteHourTimeSeries<int> TS;

  RandomInt32 random(5);

  // [10, 109], 12 buckets including above and below
  {
    TimeseriesHistogram<int> h(10, 10, 110);

    EXPECT_EQ(0, h.getPercentileEstimate(0, TS::ALLTIME));

    EXPECT_EQ(12, h.getNumBuckets());
    EXPECT_EQ(10, h.getBucketSize());
    EXPECT_EQ(10, h.getMin());
    EXPECT_EQ(110, h.getMax());

    for (size_t i = 0; i < h.getNumBuckets(); ++i) {
      EXPECT_EQ(4, h.getBucket(i).numLevels());
    }

    int maxVal = 120;
    h.addValue(0, 0);
    h.addValue(0, maxVal);
    for (int i = 0; i < 98; i++) {
      h.addValue(0, random() % maxVal);
    }

    h.update(::time(nullptr));
    // bucket 0 stores everything below min, so its minimum
    // is the lowest possible number
    EXPECT_EQ(
        std::numeric_limits<int>::min(),
        h.getPercentileBucketMin(1, TS::ALLTIME));
    EXPECT_EQ(110, h.getPercentileBucketMin(99, TS::ALLTIME));

    EXPECT_EQ(-2, h.getPercentileEstimate(0, TS::ALLTIME));
    EXPECT_EQ(-1, h.getPercentileEstimate(1, TS::ALLTIME));
    EXPECT_EQ(119, h.getPercentileEstimate(99, TS::ALLTIME));
    EXPECT_EQ(120, h.getPercentileEstimate(100, TS::ALLTIME));
  }
}

TEST(TimeseriesHistogram, String) {
  typedef MinuteTenMinuteHourTimeSeries<int> TS;

  RandomInt32 random(5);

  // [10, 109], 12 buckets including above and below
  {
    TimeseriesHistogram<int> hist(10, 10, 110);

    int maxVal = 120;
    hist.addValue(0, 0);
    hist.addValue(0, maxVal);
    for (int i = 0; i < 98; i++) {
      hist.addValue(0, random() % maxVal);
    }

    hist.update(0);

    const char* const kStringValues1[TS::NUM_LEVELS] = {
        "-2147483648:12:4,10:8:13,20:8:24,30:6:34,40:13:46,50:8:54,60:7:64,"
        "70:7:74,80:8:84,90:10:94,100:3:103,110:10:115",
        "-2147483648:12:4,10:8:13,20:8:24,30:6:34,40:13:46,50:8:54,60:7:64,"
        "70:7:74,80:8:84,90:10:94,100:3:103,110:10:115",
        "-2147483648:12:4,10:8:13,20:8:24,30:6:34,40:13:46,50:8:54,60:7:64,"
        "70:7:74,80:8:84,90:10:94,100:3:103,110:10:115",
        "-2147483648:12:4,10:8:13,20:8:24,30:6:34,40:13:46,50:8:54,60:7:64,"
        "70:7:74,80:8:84,90:10:94,100:3:103,110:10:115",
    };

    CHECK_EQ(std::size(kStringValues1), hist.getNumLevels());

    for (size_t level = 0; level < hist.getNumLevels(); ++level) {
      EXPECT_EQ(kStringValues1[level], hist.getString(level));
    }

    const char* const kStringValues2[TS::NUM_LEVELS] = {
        "-2147483648:12:4,10:8:13,20:8:24,30:6:34,40:13:46,50:8:54,60:7:64,"
        "70:7:74,80:8:84,90:10:94,100:3:103,110:10:115",
        "-2147483648:12:4,10:8:13,20:8:24,30:6:34,40:13:46,50:8:54,60:7:64,"
        "70:7:74,80:8:84,90:10:94,100:3:103,110:10:115",
        "-2147483648:12:4,10:8:13,20:8:24,30:6:34,40:13:46,50:8:54,60:7:64,"
        "70:7:74,80:8:84,90:10:94,100:3:103,110:10:115",
        "-2147483648:12:4,10:8:13,20:8:24,30:6:34,40:13:46,50:8:54,60:7:64,"
        "70:7:74,80:8:84,90:10:94,100:3:103,110:10:115",
    };

    CHECK_EQ(std::size(kStringValues2), hist.getNumLevels());

    for (size_t level = 0; level < hist.getNumLevels(); ++level) {
      EXPECT_EQ(kStringValues2[level], hist.getString(level));
    }
  }
}

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

TEST(TimeseriesHistogram, SingleUniqueValue) {
  int values[] = {-1, 0, 500, 1000, 1500};
  for (size_t ii = 0; ii < std::size(values); ++ii) {
    int value = values[ii];
    TimeseriesHistogram<int> h(10, 0, 1000);
    const int kNumIters = 1000;
    for (int jj = 0; jj < kNumIters; ++jj) {
      h.addValue(time(nullptr), value);
    }
    h.update(time(nullptr));
    // since we've only added one unique value, all percentiles should
    // be that value
    EXPECT_EQ(h.getPercentileEstimate(10, 0), value);
    EXPECT_EQ(h.getPercentileEstimate(50, 0), value);
    EXPECT_EQ(h.getPercentileEstimate(99, 0), value);

    // Things get trickier if there are multiple unique values.
    const int kNewValue = 750;
    for (int kk = 0; kk < 2 * kNumIters; ++kk) {
      h.addValue(time(nullptr), kNewValue);
    }
    h.update(time(nullptr));
    EXPECT_NEAR(h.getPercentileEstimate(50, 0), kNewValue + 5, 5);
    if (value >= 0 && value <= 1000) {
      // only do further testing if value is within our bucket range,
      // else estimates can be wildly off
      if (kNewValue > value) {
        EXPECT_NEAR(h.getPercentileEstimate(10, 0), value + 5, 5);
        EXPECT_NEAR(h.getPercentileEstimate(99, 0), kNewValue + 5, 5);
      } else {
        EXPECT_NEAR(h.getPercentileEstimate(10, 0), kNewValue + 5, 5);
        EXPECT_NEAR(h.getPercentileEstimate(99, 0), value + 5, 5);
      }
    }
  }
}
