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

#include <array>
#include <map>
#include <string>
#include <string_view>

#include <fb303/ServiceData.h>
#include <folly/Benchmark.h>
#include <folly/BenchmarkUtil.h>
#include <folly/Conv.h>
#include <folly/init/Init.h>

using namespace facebook::fb303;

namespace {

/*
 * A corpus of ~350k keys, roughly in line with the shape the counter keys are
 * seen in the wild, but not over-indexed on exactly mimicking production
 * traffic.
 *
 * Shape matters as much as size. TimeseriesExporter registers a stat's
 * whole family consecutively, so siblings share a prefix and are neighbours in
 * both registration order and sorted order. Reproducing that clustering is the
 * point: a corpus registered in random or already-sorted order would not
 * exercise the locality getValues() depends on.
 *
 * Every name runs past the 15-char SSO limit, so each key's characters live in
 * their own allocation, as they do in a real service.
 */

// A service accumulates families from every library it links, not one
// synthetic prefix.
constexpr std::array<const char*, 4> kPrefixes{
    "thrift",
    "servicerouter",
    "cachelib",
    "myservice",
};

// TimeseriesExporter spells a level as its duration in seconds, and all-time
// as no suffix at all.
constexpr std::array<const char*, 4> kLevels{".60", ".600", ".3600", ""};

// Stats declare a subset of the five export types; three is typical.
constexpr std::array<const char*, 3> kTypes{"sum", "count", "avg"};
constexpr std::array<const char*, 3> kPercentiles{"p50", "p95", "p99"};

constexpr int kTimeseriesStats = 20000; // x 12 = 240,000
constexpr int kHistograms = 6000; //       x 12 =  72,000
constexpr int kSingleCounters = 38000; //  x  1 =  38,000
constexpr size_t kNumCounters = 350000;

const char* prefixFor(int i) {
  return kPrefixes[i % kPrefixes.size()];
}

DynamicCounters& clusteredCorpus() {
  static ServiceData data;
  static const bool registered = [] {
    auto& counters = *data.getDynamicCounters();
    auto add = [&](std::string_view name) {
      counters.registerCallback(name, [] { return CounterType{1}; });
    };
    for (int stat = 0; stat < kTimeseriesStats; ++stat) {
      for (const auto* type : kTypes) {
        for (const auto* level : kLevels) {
          add(folly::to<std::string>(
              prefixFor(stat), ".endpoint", stat, ".num_calls.", type, level));
        }
      }
    }
    for (int hist = 0; hist < kHistograms; ++hist) {
      for (const auto* pct : kPercentiles) {
        for (const auto* level : kLevels) {
          add(folly::to<std::string>(
              prefixFor(hist), ".operation", hist, ".latency_us.", pct, level));
        }
      }
    }
    for (int one = 0; one < kSingleCounters; ++one) {
      add(folly::to<std::string>(prefixFor(one), ".instance", one, ".healthy"));
    }
    return true;
  }();
  (void)registered;
  return *data.getDynamicCounters();
}

} // namespace

/*
 * The output map is built and destroyed outside the measured region: both are
 * O(size) and constant across variants, so counting them would dilute the
 * signal this benchmark exists to expose.
 *
 * A service collects counters once a minute, so nothing it touches is still
 * resident by the next pass; bm_llc_evict() puts each iteration back in that
 * state, rather than letting iteration N inherit iteration N-1's cache.
 */
BENCHMARK_MULTI(GetCountersIntoEmptyMap) {
  folly::BenchmarkSuspender suspender;
  auto& counters = clusteredCorpus();
  std::map<std::string, int64_t> output;
  folly::bm_llc_evict(0);
  suspender.dismiss();

  counters.getCounters(&output);

  suspender.rehire();
  CHECK_EQ(kNumCounters, output.size());
  return 1;
}

/*
 * ServiceData::getCounters() seeds the map with flat counters and quantiles
 * before the dynamic counters land in it, so their keys are interleaved with
 * the ones being inserted here. That is the case where an insertion hint can
 * miss, which the row above never exercises.
 */
BENCHMARK_MULTI(GetCountersIntoSeededMap) {
  folly::BenchmarkSuspender suspender;
  auto& counters = clusteredCorpus();
  std::map<std::string, int64_t> output;
  for (int stat = 0; stat < kTimeseriesStats; ++stat) {
    output.emplace(
        folly::to<std::string>(
            prefixFor(stat), ".endpoint", stat, ".num_calls.flat"),
        0);
  }
  folly::bm_llc_evict(0);
  suspender.dismiss();

  counters.getCounters(&output);

  suspender.rehire();
  CHECK_EQ(kNumCounters + kTimeseriesStats, output.size());
  return 1;
}

int main(int argc, char** argv) {
  folly::Init init{&argc, &argv, true};
  folly::runBenchmarks();
  return 0;
}

/*
============================================================================
fbcode/fb303/test/GetCountersBenchmark.cpp     relative  time/iter   iters/s
============================================================================
GetCountersIntoEmptyMap                                   523.49ms      1.91
GetCountersIntoSeededMap                                  525.76ms      1.90
*/
