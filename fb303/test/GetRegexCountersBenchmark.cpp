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

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include <fmt/format.h>
#include <folly/Benchmark.h>
#include <folly/String.h>
#include <folly/init/Init.h>
#include <folly/json/DynamicConverter.h>

#include "common/stats/ServiceData.h"

using namespace folly;
using namespace facebook::stats;

ServiceData fb303Data;

constexpr int kMaxIter = 3000;
constexpr std::size_t kMediumCounterCount = 2500;
constexpr std::size_t kLargeCounterCount = 9000;

const std::string kOneOrdinaryCounterRegex =
    R"(fb303\.synthetic\.ordinary_counter\.0)";

std::string ordinaryCounterName(std::size_t index) {
  return fmt::format("fb303.synthetic.ordinary_counter.{}", index);
}

void populateOrdinaryCounters(ServiceData& serviceData, std::size_t count) {
  for (std::size_t index = 0; index < count; ++index) {
    serviceData.setCounter(
        ordinaryCounterName(index), static_cast<int64_t>(index));
  }
}

struct OrdinaryCounterFixture {
  explicit OrdinaryCounterFixture(std::size_t count)
      : presentKey{ordinaryCounterName(count - 1)},
        absentKey{ordinaryCounterName(count)} {
    populateOrdinaryCounters(serviceData, count);
    const auto selectedCount = std::min<std::size_t>(count, 8);
    selectedKeys.reserve(selectedCount);
    for (std::size_t index = 0; index < selectedCount; ++index) {
      selectedKeys.emplace_back(ordinaryCounterName(index));
    }
  }

  ServiceData serviceData;
  std::string presentKey;
  std::string absentKey;
  std::vector<std::string> selectedKeys;
};

const OrdinaryCounterFixture& ordinaryFixture(std::size_t count) {
  if (count == kMediumCounterCount) {
    static const OrdinaryCounterFixture mediumScale{kMediumCounterCount};
    return mediumScale;
  }
  CHECK_EQ(count, kLargeCounterCount);
  static const OrdinaryCounterFixture largeScale{kLargeCounterCount};
  return largeScale;
}

OrdinaryCounterFixture& mutableOrdinaryFixture(std::size_t count) {
  if (count == kMediumCounterCount) {
    static OrdinaryCounterFixture mediumScale{kMediumCounterCount};
    return mediumScale;
  }
  CHECK_EQ(count, kLargeCounterCount);
  static OrdinaryCounterFixture largeScale{kLargeCounterCount};
  return largeScale;
}

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

void SetExisting(unsigned iterations, std::size_t count) {
  BenchmarkSuspender setup;
  auto& data = mutableOrdinaryFixture(count);
  setup.dismiss();

  while (iterations-- > 0) {
    doNotOptimizeAway(data.serviceData.setCounter(data.presentKey, 1));
  }
}

void IncrementExisting(unsigned iterations, std::size_t count) {
  BenchmarkSuspender setup;
  auto& data = mutableOrdinaryFixture(count);
  setup.dismiss();

  while (iterations-- > 0) {
    doNotOptimizeAway(data.serviceData.incrementCounter(data.presentKey));
  }
}

void GetCounterIfExistsPresent(unsigned iterations, std::size_t count) {
  BenchmarkSuspender setup;
  const auto& data = ordinaryFixture(count);
  setup.dismiss();

  while (iterations-- > 0) {
    doNotOptimizeAway(data.serviceData.getCounterIfExists(data.presentKey));
  }
}

void GetCounterIfExistsAbsent(unsigned iterations, std::size_t count) {
  BenchmarkSuspender setup;
  const auto& data = ordinaryFixture(count);
  setup.dismiss();

  while (iterations-- > 0) {
    doNotOptimizeAway(data.serviceData.getCounterIfExists(data.absentKey));
  }
}

void GetSelectedCounters(unsigned iterations, std::size_t count) {
  BenchmarkSuspender setup;
  const auto& data = ordinaryFixture(count);
  std::map<std::string, int64_t> values;
  setup.dismiss();

  while (iterations-- > 0) {
    values.clear();
    data.serviceData.getSelectedCounters(values, data.selectedKeys);
    doNotOptimizeAway(values);
  }
}

void GetCounters(unsigned iterations, std::size_t count) {
  BenchmarkSuspender setup;
  const auto& data = ordinaryFixture(count);
  std::map<std::string, int64_t> values;
  setup.dismiss();

  while (iterations-- > 0) {
    values.clear();
    data.serviceData.getCounters(values);
    doNotOptimizeAway(values);
  }
}

void GetCounterKeys(unsigned iterations, std::size_t count) {
  BenchmarkSuspender setup;
  const auto& data = ordinaryFixture(count);
  setup.dismiss();

  while (iterations-- > 0) {
    doNotOptimizeAway(data.serviceData.getCounterKeys());
  }
}

void getRegexCountersCold(std::size_t count) {
  BenchmarkSuspender setup;
  ServiceData serviceData;
  populateOrdinaryCounters(serviceData, count);
  setup.dismiss();

  const auto counters = serviceData.getRegexCounters(kOneOrdinaryCounterRegex);
  doNotOptimizeAway(counters);

  setup.rehire();
  CHECK_EQ(counters.size(), 1);
}

void GetRegexCountersWarm(unsigned iterations, std::size_t count) {
  BenchmarkSuspender setup;
  const auto& data = ordinaryFixture(count);
  const auto first =
      data.serviceData.getRegexCounters(kOneOrdinaryCounterRegex);
  CHECK_EQ(first.size(), 1);
  setup.dismiss();

  while (iterations-- > 0) {
    const auto counters =
        data.serviceData.getRegexCounters(kOneOrdinaryCounterRegex);
    doNotOptimizeAway(counters);
  }
}

void InsertClear(unsigned iterations, std::size_t count) {
  BenchmarkSuspender setup;
  auto& data = mutableOrdinaryFixture(count);
  const std::string cycleKey{"fb303.synthetic.ordinary_counter.cycle"};
  setup.dismiss();

  while (iterations-- > 0) {
    data.serviceData.setCounter(cycleKey, 1);
    data.serviceData.clearCounter(cycleKey);
  }
}

BENCHMARK_NAMED_PARAM(SetExisting, Medium, kMediumCounterCount)
BENCHMARK_NAMED_PARAM(SetExisting, Large, kLargeCounterCount)
BENCHMARK_DRAW_LINE();
BENCHMARK_NAMED_PARAM(IncrementExisting, Medium, kMediumCounterCount)
BENCHMARK_NAMED_PARAM(IncrementExisting, Large, kLargeCounterCount)
BENCHMARK_DRAW_LINE();
BENCHMARK_NAMED_PARAM(GetCounterIfExistsPresent, Medium, kMediumCounterCount)
BENCHMARK_NAMED_PARAM(GetCounterIfExistsPresent, Large, kLargeCounterCount)
BENCHMARK_DRAW_LINE();
BENCHMARK_NAMED_PARAM(GetCounterIfExistsAbsent, Medium, kMediumCounterCount)
BENCHMARK_NAMED_PARAM(GetCounterIfExistsAbsent, Large, kLargeCounterCount)
BENCHMARK_DRAW_LINE();
BENCHMARK_NAMED_PARAM(GetSelectedCounters, Medium, kMediumCounterCount)
BENCHMARK_NAMED_PARAM(GetSelectedCounters, Large, kLargeCounterCount)
BENCHMARK_DRAW_LINE();
BENCHMARK_NAMED_PARAM(GetCounters, Medium, kMediumCounterCount)
BENCHMARK_NAMED_PARAM(GetCounters, Large, kLargeCounterCount)
BENCHMARK_DRAW_LINE();
BENCHMARK_NAMED_PARAM(GetCounterKeys, Medium, kMediumCounterCount)
BENCHMARK_NAMED_PARAM(GetCounterKeys, Large, kLargeCounterCount)
BENCHMARK_DRAW_LINE();
BENCHMARK(GetRegexCountersColdMedium) {
  getRegexCountersCold(kMediumCounterCount);
}
BENCHMARK(GetRegexCountersColdLarge) {
  getRegexCountersCold(kLargeCounterCount);
}
BENCHMARK_DRAW_LINE();
BENCHMARK_NAMED_PARAM(GetRegexCountersWarm, Medium, kMediumCounterCount)
BENCHMARK_NAMED_PARAM(GetRegexCountersWarm, Large, kLargeCounterCount)
BENCHMARK_DRAW_LINE();
BENCHMARK_NAMED_PARAM(InsertClear, Medium, kMediumCounterCount)
BENCHMARK_NAMED_PARAM(InsertClear, Large, kLargeCounterCount)

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
