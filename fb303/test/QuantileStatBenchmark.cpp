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

#include "common/stats/DynamicStats.h"

#include <boost/thread/barrier.hpp>
#include <fb303/ThreadCachedServiceData.h>
#include <folly/Benchmark.h>
#include <folly/init/Init.h>

using namespace facebook::fb303;

DEFINE_quantile_stat(
    basic_1_level,
    ExportTypeConsts::kAvg,
    QuantileConsts::kP99,
    SlidingWindowPeriodConsts::kOneMin);
DEFINE_quantile_stat(
    basic_3_level,
    ExportTypeConsts::kAvg,
    QuantileConsts::kP99,
    SlidingWindowPeriodConsts::kOneMinTenMinHour);
DEFINE_dynamic_quantile_stat(
    dynamic_1_level,
    "foo.{}",
    ExportTypeConsts::kAvg,
    QuantileConsts::kP99,
    SlidingWindowPeriodConsts::kOneMin);
DEFINE_dynamic_quantile_stat(
    dynamic_3_level,
    "foo.{}",
    ExportTypeConsts::kAvg,
    QuantileConsts::kP99,
    SlidingWindowPeriodConsts::kOneMinTenMinHour);

DEFINE_histogram(
    basic_histogram_1_level,
    10000,
    0,
    1000000,
    facebook::stats::AVG,
    99,
    MinuteOnlyTimeSeries<int64_t>());
DEFINE_histogram(
    basic_histogram_3_level,
    10000,
    0,
    1000000,
    facebook::stats::AVG,
    99,
    MinuteTenMinuteHourTimeSeries<int64_t>());
DEFINE_dynamic_histogram(
    dynamic_histogram_1_level,
    "bar.{}",
    10000,
    0,
    1000000,
    facebook::stats::AVG,
    99,
    MinuteOnlyTimeSeries<int64_t>());
DEFINE_dynamic_histogram(
    dynamic_histogram_3_level,
    "bar.{}",
    10000,
    0,
    1000000,
    facebook::stats::AVG,
    99,
    MinuteTenMinuteHourTimeSeries<int64_t>());

unsigned int basic(
    unsigned int iters,
    size_t nThreads,
    bool passNow,
    bool hist,
    unsigned int levels) {
  iters = 1000000;

  auto barrier = std::make_shared<boost::barrier>(nThreads + 1);

  std::vector<std::thread> threads;
  threads.reserve(nThreads);
  BENCHMARK_SUSPEND {
    for (size_t i = 0; i < nThreads; ++i) {
      threads.emplace_back([=]() {
        barrier->wait();
        auto now = std::chrono::steady_clock::now();
        for (size_t iter = 0; iter < iters; ++iter) {
          if (hist) {
            if (levels == 1) {
              STATS_basic_histogram_1_level.add(iter);
            } else {
              STATS_basic_histogram_3_level.add(iter);
            }
          } else if (passNow) {
            if (levels == 1) {
              STATS_basic_1_level.addValue(iter, now);
            } else {
              STATS_basic_3_level.addValue(iter, now);
            }
          } else {
            if (levels == 1) {
              STATS_basic_1_level.addValue(iter);
            } else {
              STATS_basic_3_level.addValue(iter);
            }
          }
        }
        barrier->wait();
      });
    }
  }

  barrier->wait();
  barrier->wait();

  BENCHMARK_SUSPEND {
    for (auto& thread : threads) {
      thread.join();
    }
  }
  return iters;
}

unsigned int dynamic(
    unsigned int iters,
    size_t nThreads,
    bool passNow,
    bool hist,
    unsigned int levels) {
  iters = 1000000;

  auto barrier = std::make_shared<boost::barrier>(nThreads + 1);

  std::vector<std::thread> threads;
  threads.reserve(nThreads);
  BENCHMARK_SUSPEND {
    for (size_t i = 0; i < nThreads; ++i) {
      threads.emplace_back([=]() {
        barrier->wait();
        auto now = std::chrono::steady_clock::now();
        for (size_t iter = 0; iter < iters; ++iter) {
          if (hist) {
            if (levels == 1) {
              STATS_dynamic_histogram_1_level.add(iter, "bar");
            } else {
              STATS_dynamic_histogram_3_level.add(iter, "bar");
            }
          } else if (passNow) {
            if (levels == 1) {
              STATS_dynamic_1_level.addValue(iter, now, "bar");
            } else {
              STATS_dynamic_3_level.addValue(iter, now, "bar");
            }
          } else {
            if (levels == 1) {
              STATS_dynamic_1_level.addValue(iter, "bar");
            } else {
              STATS_dynamic_3_level.addValue(iter, "bar");
            }
          }
        }
        barrier->wait();
      });
    }
  }

  barrier->wait();
  barrier->wait();

  BENCHMARK_SUSPEND {
    for (auto& thread : threads) {
      thread.join();
    }
  }
  return iters;
}

BENCHMARK_NAMED_PARAM_MULTI(basic, 1thread_1level_hist, 1, false, true, 1)
BENCHMARK_RELATIVE_NAMED_PARAM_MULTI(
    basic,
    1thread_1level_now,
    1,
    true,
    false,
    1)
BENCHMARK_RELATIVE_NAMED_PARAM_MULTI(basic, 1thread_1level, 1, false, false, 1)
BENCHMARK_DRAW_LINE();
BENCHMARK_NAMED_PARAM_MULTI(basic, 1thread_3level_hist, 1, false, true, 3)
BENCHMARK_RELATIVE_NAMED_PARAM_MULTI(
    basic,
    1thread_3level_now,
    1,
    true,
    false,
    3)
BENCHMARK_RELATIVE_NAMED_PARAM_MULTI(basic, 1thread_3level, 1, false, false, 3)
BENCHMARK_DRAW_LINE();
BENCHMARK_NAMED_PARAM_MULTI(basic, 16thread_1level_hist, 16, false, true, 1)
BENCHMARK_RELATIVE_NAMED_PARAM_MULTI(
    basic,
    16thread_1level_now,
    16,
    true,
    false,
    1)
BENCHMARK_RELATIVE_NAMED_PARAM_MULTI(
    basic,
    16thread_1level,
    16,
    false,
    false,
    1)
BENCHMARK_DRAW_LINE();
BENCHMARK_NAMED_PARAM_MULTI(basic, 16thread_3level_hist, 16, false, true, 3)
BENCHMARK_RELATIVE_NAMED_PARAM_MULTI(
    basic,
    16thread_3level_now,
    16,
    true,
    false,
    3)
BENCHMARK_RELATIVE_NAMED_PARAM_MULTI(
    basic,
    16thread_3level,
    16,
    false,
    false,
    3)
BENCHMARK_DRAW_LINE();
BENCHMARK_NAMED_PARAM_MULTI(basic, 32thread_1level_hist, 32, false, true, 1)
BENCHMARK_RELATIVE_NAMED_PARAM_MULTI(
    basic,
    32thread_1level_now,
    32,
    true,
    false,
    1)
BENCHMARK_RELATIVE_NAMED_PARAM_MULTI(
    basic,
    32thread_1level,
    32,
    false,
    false,
    1)
BENCHMARK_DRAW_LINE();
BENCHMARK_NAMED_PARAM_MULTI(basic, 32thread_3level_hist, 32, false, true, 3)
BENCHMARK_RELATIVE_NAMED_PARAM_MULTI(
    basic,
    32thread_3level_now,
    32,
    true,
    false,
    3)
BENCHMARK_RELATIVE_NAMED_PARAM_MULTI(
    basic,
    32thread_3level,
    32,
    false,
    false,
    3)
BENCHMARK_DRAW_LINE();
BENCHMARK_DRAW_LINE();
BENCHMARK_DRAW_LINE();
BENCHMARK_NAMED_PARAM_MULTI(dynamic, 1thread_1level_hist, 1, false, true, 1)
BENCHMARK_RELATIVE_NAMED_PARAM_MULTI(
    dynamic,
    1thread_1level_now,
    1,
    true,
    false,
    1)
BENCHMARK_RELATIVE_NAMED_PARAM_MULTI(
    dynamic,
    1thread_1level,
    1,
    false,
    false,
    1)
BENCHMARK_DRAW_LINE();
BENCHMARK_NAMED_PARAM_MULTI(dynamic, 1thread_3level_hist, 1, false, true, 3)
BENCHMARK_RELATIVE_NAMED_PARAM_MULTI(
    dynamic,
    1thread_3level_now,
    1,
    true,
    false,
    3)
BENCHMARK_RELATIVE_NAMED_PARAM_MULTI(
    dynamic,
    1thread_3level,
    1,
    false,
    false,
    3)
BENCHMARK_DRAW_LINE();
BENCHMARK_NAMED_PARAM_MULTI(dynamic, 16thread_1level_hist, 16, false, true, 1)
BENCHMARK_RELATIVE_NAMED_PARAM_MULTI(
    dynamic,
    16thread_1level_now,
    16,
    true,
    false,
    1)
BENCHMARK_RELATIVE_NAMED_PARAM_MULTI(
    dynamic,
    16thread_1level,
    16,
    false,
    false,
    1)
BENCHMARK_DRAW_LINE();
BENCHMARK_NAMED_PARAM_MULTI(dynamic, 16thread_3level_hist, 16, false, true, 3)
BENCHMARK_RELATIVE_NAMED_PARAM_MULTI(
    dynamic,
    16thread_3level_now,
    16,
    true,
    false,
    3)
BENCHMARK_RELATIVE_NAMED_PARAM_MULTI(
    dynamic,
    16thread_3level,
    16,
    false,
    false,
    3)
BENCHMARK_DRAW_LINE();
BENCHMARK_NAMED_PARAM_MULTI(dynamic, 32thread_1level_hist, 32, false, true, 1)
BENCHMARK_RELATIVE_NAMED_PARAM_MULTI(
    dynamic,
    32thread_1level_now,
    32,
    true,
    false,
    1)
BENCHMARK_RELATIVE_NAMED_PARAM_MULTI(
    dynamic,
    32thread_1level,
    32,
    false,
    false,
    1)
BENCHMARK_DRAW_LINE();
BENCHMARK_NAMED_PARAM_MULTI(dynamic, 32thread_3level_hist, 32, false, true, 3)
BENCHMARK_RELATIVE_NAMED_PARAM_MULTI(
    dynamic,
    32thread_3level_now,
    32,
    true,
    false,
    3)
BENCHMARK_RELATIVE_NAMED_PARAM_MULTI(
    dynamic,
    32thread_3level,
    32,
    false,
    false,
    3)
BENCHMARK_DRAW_LINE();

/*
 * ============================================================================
 * common/stats/test/QuantileStatBenchmark.cpp     relative  time/iter  iters/s
 * ============================================================================
 * basic(1thread_1level_hist)                                  89.06ns   11.23M
 * basic(1thread_1level_now)                        148.06%    60.15ns   16.62M
 * basic(1thread_1level)                            104.96%    84.85ns   11.79M
 * ----------------------------------------------------------------------------
 * basic(1thread_3level_hist)                                  67.99ns   14.71M
 * basic(1thread_3level_now)                         59.62%   114.04ns    8.77M
 * basic(1thread_3level)                             48.86%   139.15ns    7.19M
 * ----------------------------------------------------------------------------
 * basic(16thread_1level_hist)                                106.68ns    9.37M
 * basic(16thread_1level_now)                       136.06%    78.40ns   12.75M
 * basic(16thread_1level)                           123.26%    86.55ns   11.55M
 * ----------------------------------------------------------------------------
 * basic(16thread_3level_hist)                                 87.10ns   11.48M
 * basic(16thread_3level_now)                        57.80%   150.70ns    6.64M
 * basic(16thread_3level)                            46.43%   187.60ns    5.33M
 * ----------------------------------------------------------------------------
 * basic(32thread_1level_hist)                                162.28ns    6.16M
 * basic(32thread_1level_now)                       138.62%   117.07ns    8.54M
 * basic(32thread_1level)                           106.51%   152.36ns    6.56M
 * ----------------------------------------------------------------------------
 * basic(32thread_3level_hist)                                135.21ns    7.40M
 * basic(32thread_3level_now)                        50.38%   268.37ns    3.73M
 * basic(32thread_3level)                            41.52%   325.62ns    3.07M
 * ----------------------------------------------------------------------------
 * ----------------------------------------------------------------------------
 * ----------------------------------------------------------------------------
 * dynamic(1thread_1level_hist)                                87.91ns   11.38M
 * dynamic(1thread_1level_now)                       89.09%    98.67ns   10.14M
 * dynamic(1thread_1level)                           70.41%   124.86ns    8.01M
 * ----------------------------------------------------------------------------
 * dynamic(1thread_3level_hist)                                87.31ns   11.45M
 * dynamic(1thread_3level_now)                       96.51%    90.47ns   11.05M
 * dynamic(1thread_3level)                           73.42%   118.93ns    8.41M
 * ----------------------------------------------------------------------------
 * dynamic(16thread_1level_hist)                              100.61ns    9.94M
 * dynamic(16thread_1level_now)                      68.07%   147.79ns    6.77M
 * dynamic(16thread_1level)                          56.96%   176.62ns    5.66M
 * ----------------------------------------------------------------------------
 * dynamic(16thread_3level_hist)                              100.39ns    9.96M
 * dynamic(16thread_3level_now)                      97.88%   102.57ns    9.75M
 * dynamic(16thread_3level)                          63.53%   158.02ns    6.33M
 * ----------------------------------------------------------------------------
 * dynamic(32thread_1level_hist)                              150.44ns    6.65M
 * dynamic(32thread_1level_now)                      84.14%   178.80ns    5.59M
 * dynamic(32thread_1level)                          65.75%   228.79ns    4.37M
 * ----------------------------------------------------------------------------
 * dynamic(32thread_3level_hist)                              172.52ns    5.80M
 * dynamic(32thread_3level_now)                      96.34%   179.07ns    5.58M
 * dynamic(32thread_3level)                          78.65%   219.34ns    4.56M
 * ----------------------------------------------------------------------------
 * ============================================================================
 *
 */
int main(int argc, char* argv[]) {
  folly::init(&argc, &argv, true);
  folly::runBenchmarks();
  return 0;
}
