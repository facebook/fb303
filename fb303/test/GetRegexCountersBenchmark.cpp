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
#include <folly/DynamicConverter.h>
#include <folly/String.h>
#include <folly/init/Init.h>
#include <time.h>
#include "common/stats/ServiceData.h"

using namespace std;
using namespace facebook::fb303;
using namespace folly;
using namespace facebook::stats;
ServiceData fb303Data;
const int kMaxIter = 3000;
const int kGetRegexCountersCalls = 100;

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
BENCHMARK(GetRegexCountersBenchmarkSubset) {
  BenchmarkSuspender startup;
  prepareData();
  startup.dismiss();
  for (int iter = 0; iter < kGetRegexCountersCalls; iter++) {
    std::map<std::string, int64_t> counters =
        fb303Data.getRegexCounters("matching.*");
  }
}

// Match only one counter
BENCHMARK(GetRegexCountersBenchmarkOne) {
  BenchmarkSuspender startup;
  prepareData();
  startup.dismiss();
  for (int iter = 0; iter < kGetRegexCountersCalls; iter++) {
    std::map<std::string, int64_t> counters =
        fb303Data.getRegexCounters("matchingCounter1");
  }
}

// Match all counters
BENCHMARK(GetRegexCountersBenchmarkAll) {
  BenchmarkSuspender startup;
  prepareData();
  startup.dismiss();
  for (int iter = 0; iter < kGetRegexCountersCalls; iter++) {
    std::map<std::string, int64_t> counters = fb303Data.getRegexCounters(".*");
  }
}
int main(int argc, char** argv) {
  folly::Init init{&argc, &argv, true};
  runBenchmarks();
  return 0;
}

/*
============================================================================
[...]03/test/GetRegexCountersBenchmark.cpp     relative  time/iter   iters/s
============================================================================
GetRegexCountersBenchmarkSubset                           409.24ms      2.44
GetRegexCountersBenchmarkOne                              126.50ms      7.90
GetRegexCountersBenchmarkAll                                 1.20s   835.16m
*/
