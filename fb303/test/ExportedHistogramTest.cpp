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

#include <fb303/ExportedHistogramMapImpl.h>

#include <gtest/gtest.h>
#include <time.h>

using namespace std;
using namespace facebook;
using namespace facebook::fb303;

#define EXPECT_COUNTER(expected, counters, name)                       \
  do {                                                                 \
    int64_t counterValue;                                              \
    bool counterExists = (counters).getCounter((name), &counterValue); \
    EXPECT_TRUE(counterExists) << "counter " << name << " not found";  \
    if (counterExists) {                                               \
      EXPECT_EQ((expected), counterValue) << "counter " << name;       \
    }                                                                  \
  } while (0)

TEST(ExportedHistogram, ExportedStats) {
  DynamicCounters counters;
  DynamicStrings strings;
  ExportedHistogram defaultHistogram(10, 0, 10000);
  ExportedHistogramMapImpl histMap(&counters, &strings, defaultHistogram);

  histMap.addHistogram("hist1");
  histMap.exportPercentile("hist1", 50, 95, 99, 100);
  histMap.exportStat("hist1", COUNT, SUM, AVG, RATE);
  histMap.addHistogram("hist2");
  histMap.exportPercentile("hist2", 50, 95, 99, 100);
  histMap.exportStat("hist2", COUNT, SUM, AVG, RATE);
  // hist3 is similar to hist1 but has buckets 5x as wide
  histMap.addHistogram("hist3", 50, 0, 50000);
  histMap.exportPercentile("hist3", 50, 95, 99, 100);
  histMap.exportStat("hist3", COUNT, SUM, AVG, RATE);

  // The stats code looks at wall clock time to compute the rates.
  // In order to make sure the tests are deterministic, manually tell it what
  // time it is, and specify a time farther in the future than the actual
  // current time.  When the stats code does its own internal update from the
  // actual time, it will ignore the update if it has already seen a timestamp
  // further in the future.
  auto timeStart = ::time(nullptr) + 600;

  for (int64_t n = 0; n < 100; ++n) {
    histMap.addValue("hist1", timeStart + n, n * 10);
    histMap.addValue("hist1", timeStart + n, (n * 10) + 5);
    histMap.addValue("hist2", timeStart + n, n * 10);
    histMap.addValue("hist3", timeStart + n, n * 50);
    histMap.addValue("hist3", timeStart + n, (n * 50) + 25);
  }

  histMap.getHistogram("hist1")->update(timeStart + 99);
  histMap.getHistogram("hist2")->update(timeStart + 99);
  histMap.getHistogram("hist3")->update(timeStart + 99);

  EXPECT_COUNTER(200, counters, "hist1.count");
  EXPECT_COUNTER(200, counters, "hist1.count.3600");
  EXPECT_COUNTER(200, counters, "hist1.count.600");
  EXPECT_COUNTER(200, counters, "hist3.count");
  EXPECT_COUNTER(200, counters, "hist3.count.3600");
  EXPECT_COUNTER(200, counters, "hist3.count.600");

  EXPECT_COUNTER(99500, counters, "hist1.sum");
  EXPECT_COUNTER(99500, counters, "hist1.sum.3600");
  EXPECT_COUNTER(99500, counters, "hist1.sum.600");
  EXPECT_COUNTER(497500, counters, "hist3.sum");
  EXPECT_COUNTER(497500, counters, "hist3.sum.3600");
  EXPECT_COUNTER(497500, counters, "hist3.sum.600");

  EXPECT_COUNTER(995, counters, "hist1.rate");
  EXPECT_COUNTER(995, counters, "hist1.rate.3600");
  EXPECT_COUNTER(995, counters, "hist1.rate.600");
  EXPECT_COUNTER(4975, counters, "hist3.rate");
  EXPECT_COUNTER(4975, counters, "hist3.rate.3600");
  EXPECT_COUNTER(4975, counters, "hist3.rate.600");

  EXPECT_COUNTER(497, counters, "hist1.avg");
  EXPECT_COUNTER(497, counters, "hist1.avg.3600");
  EXPECT_COUNTER(497, counters, "hist1.avg.600");

  EXPECT_COUNTER(500, counters, "hist1.p50");
  EXPECT_COUNTER(500, counters, "hist1.p50.3600");
  EXPECT_COUNTER(500, counters, "hist1.p50.600");
  EXPECT_COUNTER(2500, counters, "hist3.p50");
  EXPECT_COUNTER(2500, counters, "hist3.p50.3600");
  EXPECT_COUNTER(2500, counters, "hist3.p50.600");

  EXPECT_COUNTER(950, counters, "hist1.p95");
  EXPECT_COUNTER(950, counters, "hist1.p95.3600");
  EXPECT_COUNTER(950, counters, "hist1.p95.600");
  EXPECT_COUNTER(4750, counters, "hist3.p95");
  EXPECT_COUNTER(4750, counters, "hist3.p95.3600");
  EXPECT_COUNTER(4750, counters, "hist3.p95.600");

  EXPECT_COUNTER(990, counters, "hist1.p99");
  EXPECT_COUNTER(990, counters, "hist1.p99.3600");
  EXPECT_COUNTER(990, counters, "hist1.p99.600");
  EXPECT_COUNTER(4950, counters, "hist3.p99");
  EXPECT_COUNTER(4950, counters, "hist3.p99.3600");
  EXPECT_COUNTER(4950, counters, "hist3.p99.600");

  // p100 gives the last non-empty bucket
  EXPECT_COUNTER(1000, counters, "hist1.p100");
  EXPECT_COUNTER(1000, counters, "hist1.p100.3600");
  EXPECT_COUNTER(1000, counters, "hist1.p100.600");
  EXPECT_COUNTER(5000, counters, "hist3.p100");
  EXPECT_COUNTER(5000, counters, "hist3.p100.3600");
  EXPECT_COUNTER(5000, counters, "hist3.p100.600");

  EXPECT_COUNTER(120, counters, "hist1.count.60");
  EXPECT_COUNTER(83700, counters, "hist1.sum.60");
  EXPECT_COUNTER(1395, counters, "hist1.rate.60");
  EXPECT_COUNTER(697, counters, "hist1.avg.60");
  EXPECT_COUNTER(700, counters, "hist1.p50.60");
  EXPECT_COUNTER(970, counters, "hist1.p95.60");
  EXPECT_COUNTER(991, counters, "hist1.p99.60");
  EXPECT_COUNTER(1000, counters, "hist1.p100.60");

  EXPECT_COUNTER(120, counters, "hist3.count.60");
  EXPECT_COUNTER(418500, counters, "hist3.sum.60");
  EXPECT_COUNTER(6975, counters, "hist3.rate.60");
  EXPECT_COUNTER(3487, counters, "hist3.avg.60");
  EXPECT_COUNTER(3500, counters, "hist3.p50.60");
  EXPECT_COUNTER(4850, counters, "hist3.p95.60");
  EXPECT_COUNTER(4959, counters, "hist3.p99.60");
  EXPECT_COUNTER(5000, counters, "hist3.p100.60");

  EXPECT_COUNTER(100, counters, "hist2.count");
  // clear the histogram for hist2 and associated callbacks. Now fetching from
  // the dynamic counters should fail.
  histMap.unexportPercentile("hist2", 50, 95, 99, 100);
  histMap.unexportStat("hist2", COUNT, SUM, AVG, RATE);
  histMap.forgetHistogramsFor("hist2");

  int64_t val = 0;
  bool counterExists = counters.getCounter("hist2.count", &val);
  EXPECT_FALSE(counterExists);
}
