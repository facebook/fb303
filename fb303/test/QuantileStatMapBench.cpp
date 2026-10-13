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
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <fmt/format.h>
#include <folly/Benchmark.h>
#include <folly/init/Init.h>

#include <fb303/detail/QuantileStatMap.h>

namespace facebook::fb303::detail {
namespace {

using namespace std::chrono_literals;

using StatMap = BasicQuantileStatMap<std::chrono::steady_clock>;
using Stat = StatMap::stat_type;

constexpr std::size_t kExpectedDerivedNamesPerBase = 24;
constexpr std::size_t kLargeBaseCount = 3172;

const std::string kOneP95PerBaseRegex =
    R"(fb303\.synthetic\.quantile_stat\..*\.p95)";

const std::vector kSlidingWindowDefs = {
    std::pair{1s, size_t(60)},
    std::pair{10s, size_t(60)},
    std::pair{60s, size_t(60)},
};

const std::vector<StatMap::StatDef> kStatDefs = {
    {ExportType::SUM, 0.0},
    {ExportType::AVG, 0.0},
    {ExportType::RATE, 0.0},
    {ExportType::PERCENT, 0.95},
    {ExportType::PERCENT, 0.99},
    {ExportType::PERCENT, 0.999},
};

std::string baseName(std::size_t index) {
  return fmt::format("fb303.synthetic.quantile_stat.{:06}", index);
}

void populate(StatMap& statMap, std::size_t baseCount) {
  for (std::size_t index = 0; index < baseCount; ++index) {
    auto stat = std::make_shared<Stat>(kSlidingWindowDefs);
    stat->addValue(static_cast<double>(index + 1));
    statMap.registerQuantileStat(baseName(index), std::move(stat), kStatDefs);
  }
  CHECK_EQ(statMap.getNumKeys(), baseCount * kExpectedDerivedNamesPerBase);
}

struct Fixture {
  explicit Fixture(std::size_t baseCount)
      : presentKey{fmt::format("{}.p95", baseName(baseCount - 1))},
        absentKey{fmt::format("{}.p95", baseName(baseCount))} {
    populate(statMap, baseCount);
    const auto selectedBaseCount = std::min<std::size_t>(baseCount, 8);
    selectedKeys.reserve(selectedBaseCount * 3);
    for (std::size_t index = 0; index < selectedBaseCount; ++index) {
      const auto base = baseName(index);
      selectedKeys.emplace_back(fmt::format("{}.p95", base));
      selectedKeys.emplace_back(fmt::format("{}.p99.60", base));
      selectedKeys.emplace_back(fmt::format("{}.sum", base));
    }
  }

  StatMap statMap;
  std::string presentKey;
  std::string absentKey;
  std::vector<std::string> selectedKeys;
};

const Fixture& fixture(std::size_t baseCount) {
  if (baseCount == 1) {
    static const Fixture oneBase{1};
    return oneBase;
  }
  CHECK_EQ(baseCount, kLargeBaseCount);
  static const Fixture largeScale{kLargeBaseCount};
  return largeScale;
}

Fixture& mutableFixture(std::size_t baseCount) {
  if (baseCount == 1) {
    static Fixture oneBase{1};
    return oneBase;
  }
  CHECK_EQ(baseCount, kLargeBaseCount);
  static Fixture largeScale{kLargeBaseCount};
  return largeScale;
}

void ContainsPresent(unsigned iterations, std::size_t baseCount) {
  folly::BenchmarkSuspender setup;
  const auto& data = fixture(baseCount);
  setup.dismiss();

  while (iterations-- > 0) {
    folly::doNotOptimizeAway(data.statMap.contains(data.presentKey));
  }
}

void ContainsAbsent(unsigned iterations, std::size_t baseCount) {
  folly::BenchmarkSuspender setup;
  const auto& data = fixture(baseCount);
  setup.dismiss();

  while (iterations-- > 0) {
    folly::doNotOptimizeAway(data.statMap.contains(data.absentKey));
  }
}

void GetValue(unsigned iterations, std::size_t baseCount) {
  folly::BenchmarkSuspender setup;
  const auto& data = fixture(baseCount);
  setup.dismiss();

  while (iterations-- > 0) {
    folly::doNotOptimizeAway(data.statMap.getValue(data.presentKey));
  }
}

void GetSelectedValues(unsigned iterations, std::size_t baseCount) {
  folly::BenchmarkSuspender setup;
  const auto& data = fixture(baseCount);
  std::map<std::string, int64_t> values;
  setup.dismiss();

  while (iterations-- > 0) {
    values.clear();
    data.statMap.getSelectedValues(values, data.selectedKeys);
    folly::doNotOptimizeAway(values);
  }
}

void GetKeys(unsigned iterations, std::size_t baseCount) {
  folly::BenchmarkSuspender setup;
  const auto& data = fixture(baseCount);
  std::vector<std::string> keys;
  keys.reserve(data.statMap.getNumKeys());
  setup.dismiss();

  while (iterations-- > 0) {
    keys.clear();
    data.statMap.getKeys(keys);
    folly::doNotOptimizeAway(keys);
  }
}

void getRegexKeysCold(std::size_t baseCount) {
  folly::BenchmarkSuspender setup;
  StatMap statMap;
  populate(statMap, baseCount);
  std::vector<std::string> keys;
  keys.reserve(baseCount);
  setup.dismiss();

  statMap.getRegexKeys(keys, kOneP95PerBaseRegex);
  folly::doNotOptimizeAway(keys);

  setup.rehire();
  CHECK_EQ(keys.size(), baseCount);
}

void GetRegexKeysWarm(unsigned iterations, std::size_t baseCount) {
  folly::BenchmarkSuspender setup;
  const auto& data = fixture(baseCount);
  std::vector<std::string> keys;
  keys.reserve(baseCount);
  data.statMap.getRegexKeys(keys, kOneP95PerBaseRegex);
  CHECK_EQ(keys.size(), baseCount);
  setup.dismiss();

  while (iterations-- > 0) {
    keys.clear();
    data.statMap.getRegexKeys(keys, kOneP95PerBaseRegex);
    folly::doNotOptimizeAway(keys);
  }
}

void RegisterForget(unsigned iterations, std::size_t baseCount) {
  folly::BenchmarkSuspender setup;
  auto& data = mutableFixture(baseCount);
  auto stat = std::make_shared<Stat>(kSlidingWindowDefs);
  const std::string cycleBase{"fb303.synthetic.quantile_stat.cycle"};
  setup.dismiss();

  while (iterations-- > 0) {
    data.statMap.registerQuantileStat(cycleBase, stat, kStatDefs);
    data.statMap.forgetStatsFor(cycleBase);
  }
}

BENCHMARK_PARAM(ContainsPresent, 1)
BENCHMARK_NAMED_PARAM(ContainsPresent, Large, kLargeBaseCount)
BENCHMARK_DRAW_LINE();
BENCHMARK_PARAM(ContainsAbsent, 1)
BENCHMARK_NAMED_PARAM(ContainsAbsent, Large, kLargeBaseCount)
BENCHMARK_DRAW_LINE();
BENCHMARK_PARAM(GetValue, 1)
BENCHMARK_NAMED_PARAM(GetValue, Large, kLargeBaseCount)
BENCHMARK_DRAW_LINE();
BENCHMARK_PARAM(GetSelectedValues, 1)
BENCHMARK_NAMED_PARAM(GetSelectedValues, Large, kLargeBaseCount)
BENCHMARK_DRAW_LINE();
BENCHMARK_PARAM(GetKeys, 1)
BENCHMARK_NAMED_PARAM(GetKeys, Large, kLargeBaseCount)
BENCHMARK_DRAW_LINE();
BENCHMARK(GetRegexKeysColdOneBase) {
  getRegexKeysCold(1);
}
BENCHMARK(GetRegexKeysColdLarge) {
  getRegexKeysCold(kLargeBaseCount);
}
BENCHMARK_DRAW_LINE();
BENCHMARK_PARAM(GetRegexKeysWarm, 1)
BENCHMARK_NAMED_PARAM(GetRegexKeysWarm, Large, kLargeBaseCount)
BENCHMARK_DRAW_LINE();
BENCHMARK_PARAM(RegisterForget, 1)
BENCHMARK_NAMED_PARAM(RegisterForget, Large, kLargeBaseCount)

} // namespace
} // namespace facebook::fb303::detail

int main(int argc, char** argv) {
  folly::Init init{&argc, &argv, true};
  folly::runBenchmarks();
  return 0;
}
