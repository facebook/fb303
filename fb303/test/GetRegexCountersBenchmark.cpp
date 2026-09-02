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

#include <folly/Benchmark.h>
#include <folly/String.h>
#include <folly/init/Init.h>
#include <folly/json/DynamicConverter.h>
#include <ctime>
#include <vector>
#include "common/stats/ServiceData.h"

using namespace folly;
using namespace facebook::stats;

ServiceData fb303Data;

constexpr int kMaxIter = 3000;

// sets up service data and initializes the counters
void prepareData() {
  fb303Data.resetAllData();
  for (int iter = 0; iter < kMaxIter; iter++) {
    auto counterName = "matchingCounter" + folly::convertTo<std::string>(iter);
    fb303Data.setCounter(counterName, iter);
  }
  for (int iter = 0; iter < 2 * kMaxIter; iter++) {
    auto counterName = "counter" + folly::convertTo<std::string>(iter);
    fb303Data.setCounter(counterName, iter);
  }
}
/* It calls getRegexCounter - first call will trigger caching
 * Subsequent calls (for kGetRegexCountersIter-1) are optimized
 */
BENCHMARK(GetRegexCountersBenchmarkSubset, iters) {
  BenchmarkSuspender startup;
  prepareData();
  startup.dismiss();
  for (int iter = 0; iter < iters; iter++) {
    std::map<std::string, int64_t> counters =
        fb303Data.getRegexCounters("matching.*");
  }
}

// Match only one counter
BENCHMARK(GetRegexCountersBenchmarkOne, iters) {
  BenchmarkSuspender startup;
  prepareData();
  startup.dismiss();
  for (int iter = 0; iter < iters; iter++) {
    std::map<std::string, int64_t> counters =
        fb303Data.getRegexCounters("matchingCounter1");
  }
}

// Match all counters
BENCHMARK(GetRegexCountersBenchmarkAll, iters) {
  BenchmarkSuspender startup;
  prepareData();
  startup.dismiss();
  for (int iter = 0; iter < iters; iter++) {
    std::map<std::string, int64_t> counters = fb303Data.getRegexCounters(".*");
  }
}

/* setCounter() populates counters_ only, so the rows above never exercise
 * dynamicCounters_. A fresh ServiceData per invocation keeps the build cold --
 * a reused map would time a cached read.
 *
 * The regex is deliberately trivial. The off-lock build adds O(keys)
 * bookkeeping whose cost does not depend on match cost, so a cheap regex
 * maximizes that signal; a caller with an expensive regex sees a proportionally
 * smaller delta.
 */
constexpr int kColdBuildIter = 30000;

BENCHMARK_MULTI(GetRegexKeysColdBuildDynamic) {
  BenchmarkSuspender startup;
  ServiceData serviceData;
  DynamicCounters& dynamicCounters = *serviceData.getDynamicCounters();
  for (int iter = 0; iter < kColdBuildIter; iter++) {
    dynamicCounters.registerCallback(
        "matchingCounter" + folly::convertTo<std::string>(iter),
        [iter] { return static_cast<CounterType>(iter); });
  }
  for (int iter = 0; iter < 2 * kColdBuildIter; iter++) {
    dynamicCounters.registerCallback(
        "counter" + folly::convertTo<std::string>(iter),
        [iter] { return static_cast<CounterType>(iter); });
  }
  startup.dismiss();

  std::vector<std::string> keys;
  dynamicCounters.getRegexKeys(keys, "matching.*");

  startup.rehire();
  CHECK_EQ(static_cast<size_t>(kColdBuildIter), keys.size());
  return 1;
}

int main(int argc, char** argv) {
  folly::Init init{&argc, &argv, true};
  runBenchmarks();
  return 0;
}

/*
Results from 20-core (40-thread) Intel(R) Xeon(R) Gold 6138 CPU @ 2.00GHz
============================================================================
[...]03/test/GetRegexCountersBenchmark.cpp     relative  time/iter   iters/s
============================================================================
GetRegexCountersBenchmarkSubset                             1.53ms    651.80
GetRegexCountersBenchmarkOne                              418.52ns     2.39M
GetRegexCountersBenchmarkAll                                5.36ms    186.40
*/
