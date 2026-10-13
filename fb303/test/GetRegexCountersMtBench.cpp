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
#include <array>
#include <cstddef>
#include <cstdint>
#include <numeric>
#include <string>
#include <thread>
#include <vector>

#include <fmt/format.h>
#include <folly/Benchmark.h>
#include <folly/Random.h>
#include <folly/init/Init.h>
#include <folly/synchronization/Latch.h>

#include <fb303/ServiceData.h>

using facebook::fb303::ServiceData;

DEFINE_uint32(threads, 32, "Number of threads");
DEFINE_uint32(cycles_per_thread, 10, "100-operation cycles per thread");
DEFINE_uint32(counters, 20000, "Number of unique counters");
DEFINE_uint32(workload_seed, 1, "Seed for deterministic operation plans");
DEFINE_string(prefix, "sso_spills_over_counter_", "Prefix of counter names");

namespace {

constexpr std::size_t kOperationsPerCycle = 100;
constexpr std::uint8_t kClearSlots = 1;
constexpr std::uint8_t kRegexSlots = 20;

const std::string kRegex0 =
    "(?!(bad.*$)|(.*unwanted$)|(.*terrible.*$)|(.*counter_[0-9]_from_hell$)|(.*worst.*$))((good.*)|(.*better.*)|(.*best)|(.*counter_[0-9]*0)|(.*desired.*))";
const std::string kRegex1 =
    "(?!(bad.*$)|(.*unwanted$)|(.*terrible.*$)|(.*counter_[0-9]_from_hell$)|(.*worst.*$))((good.*)|(.*better.*)|(.*best)|(.*counter_[0-9]*1)|(.*desired.*))";

std::vector<std::string> makeCounterNames() {
  std::vector<std::string> names;
  names.reserve(FLAGS_counters);
  for (std::uint32_t index = 0; index < FLAGS_counters; ++index) {
    names.emplace_back(fmt::format("{}{}", FLAGS_prefix, index));
  }
  return names;
}

void populate(
    ServiceData& serviceData,
    const std::vector<std::string>& counterNames) {
  for (std::size_t index = 0; index < counterNames.size(); ++index) {
    serviceData.setCounter(counterNames[index], static_cast<int64_t>(index));
  }
}

struct PlannedOperation {
  std::uint8_t mixSlot;
  std::uint32_t counterIndex;
};

std::vector<PlannedOperation> makePlan(std::uint32_t threadIndex) {
  folly::Random::DefaultGenerator rng;
  rng.seed(FLAGS_workload_seed + threadIndex);

  std::array<std::uint8_t, kOperationsPerCycle> mixSlots{};
  std::iota(mixSlots.begin(), mixSlots.end(), std::uint8_t{0});

  std::vector<PlannedOperation> plan;
  plan.reserve(FLAGS_cycles_per_thread * kOperationsPerCycle);
  for (std::uint32_t cycle = 0; cycle < FLAGS_cycles_per_thread; ++cycle) {
    std::shuffle(mixSlots.begin(), mixSlots.end(), rng);
    for (const auto mixSlot : mixSlots) {
      plan.push_back({
          .mixSlot = mixSlot,
          .counterIndex = folly::Random::rand32(FLAGS_counters, rng),
      });
    }
  }
  return plan;
}

std::uint32_t runPlan(
    ServiceData& serviceData,
    const std::vector<std::string>& counterNames,
    const std::vector<PlannedOperation>& plan) {
  std::uint32_t errors = 0;
  for (const auto& operation : plan) {
    if (operation.mixSlot < kClearSlots) {
      serviceData.clearCounter(counterNames[operation.counterIndex]);
    } else if (operation.mixSlot < kClearSlots + kRegexSlots) {
      const auto& regex = (operation.mixSlot & 1) ? kRegex1 : kRegex0;
      const auto counters = serviceData.getRegexCounters(regex);
      if (counters.empty()) {
        ++errors;
      } else {
        for (const auto& [key, value] : counters) {
          if (value < 0 ||
              static_cast<std::size_t>(value) >= counterNames.size() ||
              key != counterNames[static_cast<std::size_t>(value)]) {
            ++errors;
          }
        }
      }
    } else {
      serviceData.setCounter(
          counterNames[operation.counterIndex], operation.counterIndex);
    }
  }
  return errors;
}

} // namespace

BENCHMARK_MULTI(MutationStress) {
  folly::BenchmarkSuspender suspender;
  CHECK_GT(FLAGS_threads, 0);
  CHECK_GT(FLAGS_counters, 0);
  CHECK_GT(FLAGS_cycles_per_thread, 0);

  ServiceData serviceData;
  const auto counterNames = makeCounterNames();
  populate(serviceData, counterNames);

  std::vector<std::vector<PlannedOperation>> plans;
  plans.reserve(FLAGS_threads);
  for (std::uint32_t index = 0; index < FLAGS_threads; ++index) {
    plans.emplace_back(makePlan(index));
  }

  folly::Latch ready{FLAGS_threads};
  folly::Latch start{FLAGS_threads + 1};
  std::vector<std::uint32_t> errorCounts(FLAGS_threads);
  std::vector<std::jthread> threads;
  threads.reserve(FLAGS_threads);
  for (std::uint32_t index = 0; index < FLAGS_threads; ++index) {
    threads.emplace_back([&, index] {
      ready.count_down();
      start.arrive_and_wait();
      errorCounts[index] = runPlan(serviceData, counterNames, plans[index]);
    });
  }
  ready.wait();
  suspender.dismiss();

  start.arrive_and_wait();
  for (auto& thread : threads) {
    thread.join();
  }

  suspender.rehire();
  CHECK_EQ(
      std::accumulate(errorCounts.begin(), errorCounts.end(), std::uint64_t{0}),
      0);
  return static_cast<unsigned>(
      FLAGS_threads * FLAGS_cycles_per_thread * kOperationsPerCycle);
}

int main(int argc, char** argv) {
  folly::Init init(&argc, &argv, true);
  folly::runBenchmarks();
  return 0;
}
