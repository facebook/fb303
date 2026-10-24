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
#include <map>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <fb303/CallbackValuesMap.h>
#include <fmt/core.h>
#include <folly/Benchmark.h>
#include <folly/BenchmarkUtil.h>
#include <folly/algorithm/BinaryHeap.h>
#include <folly/container/Reserve.h>
#include <folly/hash/Hash.h>
#include <folly/init/Init.h>

using CounterType = int64_t;
using CallbackMap = facebook::fb303::CallbackValuesMap<CounterType>;
using Callback = CallbackMap::Callback;
using Entry = CallbackMap::CallbackEntry;
using EntryPtr = std::shared_ptr<Entry>;
struct CachedEntry {
  std::string_view name;
  EntryPtr entry;
};
using ValuesMap = CallbackMap::ValuesMap;

namespace {

constexpr size_t kNumShards = 128;
constexpr size_t kFamilySize = 12;

constexpr std::array<const char*, 4> kPrefixes{
    "thrift",
    "servicerouter",
    "cachelib",
    "myservice",
};
constexpr std::array<const char*, 4> kLevels{".60", ".600", ".3600", ""};
constexpr std::array<const char*, 3> kTypes{"sum", "count", "avg"};
constexpr std::array<const char*, 3> kPercentiles{"p50", "p95", "p99"};

const char* prefixFor(size_t index) {
  return kPrefixes[index % kPrefixes.size()];
}

size_t shardFor(folly::StringPiece name) {
  using Hash = folly::HeterogeneousAccessHash<folly::StringPiece>;
  return folly::hash::twang_mix64(Hash{}(name)) % kNumShards;
}

struct Corpus {
  std::vector<std::vector<EntryPtr>> shards;
  size_t size = 0;
};

Corpus buildCorpus(size_t total) {
  Corpus corpus;
  corpus.shards.resize(kNumShards);
  auto add = [&](std::string name) {
    corpus.shards[shardFor(name)].push_back(
        std::make_shared<Entry>(
            std::move(name), Callback{[] { return CounterType{1}; }}));
    ++corpus.size;
  };

  const size_t timeseries = total * 240 / 350 / kFamilySize * kFamilySize;
  const size_t histograms = total * 72 / 350 / kFamilySize * kFamilySize;
  for (size_t index = 0; index < timeseries; ++index) {
    const size_t family = index / kFamilySize;
    const size_t offset = index % kFamilySize;
    add(fmt::format(
        "{}.endpoint{}.num_calls.{}{}",
        prefixFor(family),
        family,
        kTypes[offset / kLevels.size()],
        kLevels[offset % kLevels.size()]));
  }
  for (size_t index = 0; index < histograms; ++index) {
    const size_t family = index / kFamilySize;
    const size_t offset = index % kFamilySize;
    add(fmt::format(
        "{}.operation{}.latency_us.{}{}",
        prefixFor(family),
        family,
        kPercentiles[offset / kLevels.size()],
        kLevels[offset % kLevels.size()]));
  }
  for (size_t index = timeseries + histograms; index < total; ++index) {
    add(fmt::format("{}.instance{}.healthy", prefixFor(index), index));
  }
  return corpus;
}

const Corpus& corpusFor(size_t total) {
  static size_t cachedSize = 0;
  static std::unique_ptr<Corpus> cached;
  if (!cached || cachedSize != total) {
    cached = std::make_unique<Corpus>(buildCorpus(total));
    cachedSize = total;
  }
  return *cached;
}

const std::string& nameOf(const EntryPtr& entry) {
  return entry->name();
}

std::string_view nameOf(const CachedEntry& entry) {
  return entry.name;
}

const EntryPtr& callbackOf(const EntryPtr& entry) {
  return entry;
}

const EntryPtr& callbackOf(const CachedEntry& entry) {
  return entry.entry;
}

std::vector<std::vector<EntryPtr>> copyAndSortShards(const Corpus& corpus) {
  std::vector<std::vector<EntryPtr>> shards;
  shards.reserve(corpus.shards.size());
  for (const auto& source : corpus.shards) {
    auto entries = source;
    std::sort(entries.begin(), entries.end(), [](const auto& a, const auto& b) {
      return nameOf(a) < nameOf(b);
    });
    shards.push_back(std::move(entries));
  }
  return shards;
}

std::vector<std::vector<CachedEntry>> cacheAndSortShards(const Corpus& corpus) {
  std::vector<std::vector<CachedEntry>> shards;
  shards.reserve(corpus.shards.size());
  for (const auto& source : corpus.shards) {
    std::vector<CachedEntry> entries;
    entries.reserve(source.size());
    for (const auto& entry : source) {
      entries.push_back({entry->name(), entry});
    }
    std::sort(entries.begin(), entries.end(), [](const auto& a, const auto& b) {
      return nameOf(a) < nameOf(b);
    });
    shards.push_back(std::move(entries));
  }
  return shards;
}

template <typename SnapshotEntry>
void mergeShards(
    ValuesMap* output,
    std::span<const std::vector<SnapshotEntry>> shards) {
  std::vector<std::span<const SnapshotEntry>> heap;
  heap.reserve(shards.size());
  for (const auto& entries : shards) {
    if (!entries.empty()) {
      heap.emplace_back(entries);
    }
  }
  auto cmp = [](const auto& a, const auto& b) {
    return nameOf(a.front()) > nameOf(b.front());
  };
  std::make_heap(heap.begin(), heap.end(), cmp);

  auto hint = output->begin();
  while (!heap.empty()) {
    const auto& entry = callbackOf(heap.front().front());
    CounterType result;
    if (entry->getValue(&result)) {
      hint = output->insert_or_assign(hint, entry->name(), std::move(result));
    }
    heap.front() = heap.front().subspan(1);
    if (heap.front().empty()) {
      std::pop_heap(heap.begin(), heap.end(), cmp);
      heap.pop_back();
    } else {
      folly::down_heap(heap.begin(), heap.end(), cmp);
    }
  }
}

void uncachedKWay(ValuesMap* output, const Corpus& corpus) {
  auto shards = copyAndSortShards(corpus);
  mergeShards(output, std::span{std::as_const(shards)});
}

void cachedKWay(ValuesMap* output, const Corpus& corpus) {
  auto shards = cacheAndSortShards(corpus);
  mergeShards(output, std::span{std::as_const(shards)});
}

std::vector<EntryPtr> copyEntries(const Corpus& corpus) {
  std::vector<EntryPtr> entries;
  for (const auto& shard : corpus.shards) {
    folly::grow_capacity_by(entries, shard.size());
    for (const auto& entry : shard) {
      entries.push_back(entry);
    }
  }
  return entries;
}

std::vector<CachedEntry> cacheEntries(const Corpus& corpus) {
  std::vector<CachedEntry> entries;
  for (const auto& shard : corpus.shards) {
    folly::grow_capacity_by(entries, shard.size());
    for (const auto& entry : shard) {
      entries.push_back({entry->name(), entry});
    }
  }
  return entries;
}

template <typename SnapshotEntry>
void globalSort(ValuesMap* output, std::vector<SnapshotEntry> entries) {
  std::sort(entries.begin(), entries.end(), [](const auto& a, const auto& b) {
    return nameOf(a) < nameOf(b);
  });

  auto hint = output->begin();
  for (const auto& snapshotEntry : entries) {
    const auto& entry = callbackOf(snapshotEntry);
    CounterType result;
    if (entry->getValue(&result)) {
      hint = output->insert_or_assign(hint, entry->name(), std::move(result));
    }
  }
}

void uncachedGlobalSort(ValuesMap* output, const Corpus& corpus) {
  globalSort(output, copyEntries(corpus));
}

void cachedGlobalSort(ValuesMap* output, const Corpus& corpus) {
  globalSort(output, cacheEntries(corpus));
}

using Merge = void (*)(ValuesMap*, const Corpus&);

unsigned benchmarkMerge(unsigned iterations, size_t total, Merge merge) {
  folly::BenchmarkSuspender suspender;
  const auto& corpus = corpusFor(total);
  for (unsigned iteration = 0; iteration < iterations; ++iteration) {
    ValuesMap output;
    folly::bm_llc_evict(0);
    suspender.dismiss();

    merge(&output, corpus);

    suspender.rehire();
    CHECK_EQ(total, output.size());
  }
  return iterations;
}

unsigned uncachedKWayBenchmark(unsigned iterations, size_t total) {
  return benchmarkMerge(iterations, total, uncachedKWay);
}

unsigned cachedKWayBenchmark(unsigned iterations, size_t total) {
  return benchmarkMerge(iterations, total, cachedKWay);
}

unsigned uncachedGlobalSortBenchmark(unsigned iterations, size_t total) {
  return benchmarkMerge(iterations, total, uncachedGlobalSort);
}

unsigned cachedGlobalSortBenchmark(unsigned iterations, size_t total) {
  return benchmarkMerge(iterations, total, cachedGlobalSort);
}

} // namespace

// Each size group is relative to the uncached k-way implementation.
BENCHMARK_NAMED_PARAM_MULTI(uncachedKWayBenchmark, 65k, 65'536)
BENCHMARK_RELATIVE_NAMED_PARAM_MULTI(cachedKWayBenchmark, 65k, 65'536)
BENCHMARK_RELATIVE_NAMED_PARAM_MULTI(uncachedGlobalSortBenchmark, 65k, 65'536)
BENCHMARK_RELATIVE_NAMED_PARAM_MULTI(cachedGlobalSortBenchmark, 65k, 65'536)
BENCHMARK_DRAW_LINE();
BENCHMARK_NAMED_PARAM_MULTI(uncachedKWayBenchmark, 262k, 262'144)
BENCHMARK_RELATIVE_NAMED_PARAM_MULTI(cachedKWayBenchmark, 262k, 262'144)
BENCHMARK_RELATIVE_NAMED_PARAM_MULTI(uncachedGlobalSortBenchmark, 262k, 262'144)
BENCHMARK_RELATIVE_NAMED_PARAM_MULTI(cachedGlobalSortBenchmark, 262k, 262'144)
BENCHMARK_DRAW_LINE();
BENCHMARK_NAMED_PARAM_MULTI(uncachedKWayBenchmark, 350k, 350'000)
BENCHMARK_RELATIVE_NAMED_PARAM_MULTI(cachedKWayBenchmark, 350k, 350'000)
BENCHMARK_RELATIVE_NAMED_PARAM_MULTI(uncachedGlobalSortBenchmark, 350k, 350'000)
BENCHMARK_RELATIVE_NAMED_PARAM_MULTI(cachedGlobalSortBenchmark, 350k, 350'000)
BENCHMARK_DRAW_LINE();
BENCHMARK_NAMED_PARAM_MULTI(uncachedKWayBenchmark, 524k, 524'288)
BENCHMARK_RELATIVE_NAMED_PARAM_MULTI(cachedKWayBenchmark, 524k, 524'288)
BENCHMARK_RELATIVE_NAMED_PARAM_MULTI(uncachedGlobalSortBenchmark, 524k, 524'288)
BENCHMARK_RELATIVE_NAMED_PARAM_MULTI(cachedGlobalSortBenchmark, 524k, 524'288)
BENCHMARK_DRAW_LINE();
BENCHMARK_NAMED_PARAM_MULTI(uncachedKWayBenchmark, 800k, 800'000)
BENCHMARK_RELATIVE_NAMED_PARAM_MULTI(cachedKWayBenchmark, 800k, 800'000)
BENCHMARK_RELATIVE_NAMED_PARAM_MULTI(uncachedGlobalSortBenchmark, 800k, 800'000)
BENCHMARK_RELATIVE_NAMED_PARAM_MULTI(cachedGlobalSortBenchmark, 800k, 800'000)
BENCHMARK_DRAW_LINE();
BENCHMARK_NAMED_PARAM_MULTI(uncachedKWayBenchmark, 1m, 1'048'576)
BENCHMARK_RELATIVE_NAMED_PARAM_MULTI(cachedKWayBenchmark, 1m, 1'048'576)
BENCHMARK_RELATIVE_NAMED_PARAM_MULTI(uncachedGlobalSortBenchmark, 1m, 1'048'576)
BENCHMARK_RELATIVE_NAMED_PARAM_MULTI(cachedGlobalSortBenchmark, 1m, 1'048'576)
BENCHMARK_DRAW_LINE();
BENCHMARK_NAMED_PARAM_MULTI(uncachedKWayBenchmark, 2m, 2'097'152)
BENCHMARK_RELATIVE_NAMED_PARAM_MULTI(cachedKWayBenchmark, 2m, 2'097'152)
BENCHMARK_RELATIVE_NAMED_PARAM_MULTI(uncachedGlobalSortBenchmark, 2m, 2'097'152)
BENCHMARK_RELATIVE_NAMED_PARAM_MULTI(cachedGlobalSortBenchmark, 2m, 2'097'152)

int main(int argc, char** argv) {
  folly::Init init{&argc, &argv, true};
  folly::runBenchmarks();
  return 0;
}
