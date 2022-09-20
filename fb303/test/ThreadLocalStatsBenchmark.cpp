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

#include <fb303/ThreadLocalStats.h>

#include <folly/Benchmark.h>
#include <folly/init/Init.h>

using namespace facebook::fb303;

namespace {
ServiceData sdata;

enum class StatType {
  kCounter = 0,
  kTimeseries = 1,
  kHistogram = 2,
};

template <typename LockTraits>
void incrementBenchmark(StatType type, uint32_t iters);

void incrementBenchmarkLocking(StatType type, uint32_t iters) {
  incrementBenchmark<TLStatsThreadSafe>(type, iters);
}

void incrementBenchmarkNoLocking(StatType type, uint32_t iters) {
  incrementBenchmark<TLStatsNoLocking>(type, iters);
}
} // namespace

BENCHMARK_DRAW_LINE();

BENCHMARK(incrementBenchmarkLocking_Counter, iters) {
  incrementBenchmarkLocking(StatType::kCounter, iters);
}
BENCHMARK(incrementBenchmarkLocking_Timeseries, iters) {
  incrementBenchmarkLocking(StatType::kTimeseries, iters);
}
BENCHMARK(incrementBenchmarkLocking_Histogram, iters) {
  incrementBenchmarkLocking(StatType::kHistogram, iters);
}

BENCHMARK_DRAW_LINE();

BENCHMARK(incrementBenchmarkNoLocking_Counter, iters) {
  incrementBenchmarkNoLocking(StatType::kCounter, iters);
}
BENCHMARK(incrementBenchmarkNoLocking_Timeseries, iters) {
  incrementBenchmarkNoLocking(StatType::kTimeseries, iters);
}
BENCHMARK(incrementBenchmarkNoLocking_Histogram, iters) {
  incrementBenchmarkNoLocking(StatType::kHistogram, iters);
}

BENCHMARK_DRAW_LINE();

int main(int argc, char* argv[]) {
  folly::init(&argc, &argv, true);
  folly::runBenchmarks();
  return 0;
}

namespace {
const int kTimeseriesIntervals[] = {5, 15, 60};
constexpr std::array<int, 32> kValues = {
    1,    9028, 532523, 653, 543,   235749, 42,    543, 523,   64,     32532,
    5364, 6436, 90879,  625, 1975,  52079,  352,   89,  62307, 970976, 23,
    5236, 970,  53256,  525, 23665, 52,     65262, 522, 8989,  622};

template <typename LockTraits>
struct State {
  explicit State(ServiceData* serviceData)
      : stats(serviceData),
        timeseries(
            &stats,
            "timeseries",
            (size_t)60,
            (size_t)2,
            kTimeseriesIntervals),
        counter(&stats, "counter"),
        histogram(
            &stats,
            "histogram",
            10,
            0,
            1000,
            AVG,
            COUNT,
            SUM,
            50,
            95,
            99) {}

  ServiceData* data;
  ThreadLocalStatsT<LockTraits> stats;
  TLTimeseriesT<LockTraits> timeseries;
  TLCounterT<LockTraits> counter;
  TLHistogramT<LockTraits> histogram;
};

template <typename LockTraits>
void incrementBenchmark(StatType type, uint32_t iters) {
  std::unique_ptr<State<LockTraits>> state;
  BENCHMARK_SUSPEND {
    state = std::make_unique<State<LockTraits>>(&sdata);
  }

  switch (type) {
    case StatType::kCounter:
      for (int idx = 0; idx < iters; idx++) {
        state->counter.incrementValue(idx);
        folly::doNotOptimizeAway(state->counter.value());
      }
      break;
    case StatType::kTimeseries:
      for (int idx = 0; idx < iters; idx++) {
        state->timeseries.addValue(idx);
        folly::doNotOptimizeAway(state->timeseries.count());
        folly::doNotOptimizeAway(state->timeseries.sum());
      }

      break;
    case StatType::kHistogram:
      for (int idx = 0; idx < iters; idx++) {
        // Histograms are bucketed, so to avoid overly optimistic results, use a
        // range of values to scatter over buckets
        state->histogram.addValue(kValues[idx % kValues.size()]);
        folly::doNotOptimizeAway(state->histogram);
      }
      break;
  }

  folly::doNotOptimizeAway(state);

  BENCHMARK_SUSPEND {
    state.reset();
  }
}
} // namespace
