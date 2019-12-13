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

#include <boost/lexical_cast.hpp>

#include <fb303/ExportedHistogramMapImpl.h>
#include <fb303/ExportedStatMapImpl.h>
#include <folly/Benchmark.h>
#include <folly/init/Init.h>
#include <functional>

#include <time.h>
using namespace std;
using namespace facebook;
using namespace facebook::fb303;
using namespace folly;

void ExportedPerformace(int numUpdates, bool useStatPtr) {
  BenchmarkSuspender braces;

  // === Initializations
  DynamicCounters dc;
  ExportedStatMapImpl statMap(&dc);

  const int kNumKeysExp = 10;
  const int kNumKeys = 1 << kNumKeysExp;
  const int kNumKeysMask = kNumKeys - 1;

  // Create the keys
  vector<string> keys(kNumKeys, "application.module.counter.");
  for (int k = 0; k < kNumKeys; k++) {
    keys[k].append(boost::lexical_cast<string>(k));
  }

  int64_t now = ::time(nullptr);

  // === Actual updates
  if (useStatPtr) {
    // Get StatPtr and add value via that
    vector<ExportedStatMapImpl::StatPtr> items(kNumKeys);
    for (int k = 0; k < kNumKeys; k++) {
      items[k] = statMap.getStatPtr(keys[k]);
    }

    braces.dismissing([&] {
      int k = 0;
      for (int u = 0; u < numUpdates; u++) {
        statMap.addValue(items[k], now, 10);
        k = ((k + 1) & kNumKeysMask);
      }
    });
  } else {
    // Directly update the values of the keys
    braces.dismissing([&] {
      int k = 0;
      for (int u = 0; u < numUpdates; u++) {
        statMap.addValue(keys[k], now, 10);
        k = ((k + 1) & kNumKeysMask);
      }
    });
  }

  // Trigger flush() to make sure things are working.
  // But don't count it in the benchmark.
  int64_t total = 0;
  for (int k = 0; k < kNumKeys; k++) {
    total += statMap.getLockedStatPtr(keys[k])->sum(0);
  }
  if (total == 0) {
    LOG(ERROR) << "Possibly something is wrong in " << __func__ << ".";
  }
}

BENCHMARK(ExportedBasicsPerformance, numUpdates) {
  ExportedPerformace(numUpdates, false);
}

BENCHMARK(ExportedLockAndUpdatePerformance, numUpdates) {
  ExportedPerformace(numUpdates, true);
}

template <typename F>
static void runInThreads(size_t kThreads, F f) {
  vector<thread> threads(kThreads);
  for (auto& th : threads) {
    th = thread(f);
  }
  for (auto& th : threads) {
    th.join();
  }
}

void MultiThreadedStatOperation(int iters, size_t kThreads) {
  DynamicCounters dc_;
  ExportedStatMapImpl statMap_(&dc_);

  runInThreads(kThreads, [=, &statMap_] {
    auto i = iters;
    while (i--) {
      auto iter = iters - i;
      int64_t now = ::time(nullptr);
      statMap_.addValue("random_app_foobar_avg_1", now, iter);
      statMap_.addValue("random_app_foobar_avg_2", now, iter * 100);
    }
  });
}

void MultiThreadedHistogramOperation(int iters, size_t kThreads) {
  DynamicCounters dc_;
  DynamicStrings ds_;
  ExportedHistogram baseHist_(1000, 0, 100000);
  ExportedHistogramMapImpl histMap_(&dc_, &ds_, baseHist_);
  std::hash<thread::id> h;

  runInThreads(kThreads, [=, &histMap_] {
    auto k = h(this_thread::get_id()) % 1000;
    auto i = iters;
    while (i--) {
      auto iter = iters - i;
      int64_t now = ::time(nullptr);
      int64_t value = 1000 * ((k + iter) % 100);
      histMap_.addValue("random_app_foobar_hist_1", now, value);
      histMap_.addValue("random_app_foobar_hist_2", now, value);
    }
  });
}

BENCHMARK_PARAM(MultiThreadedStatOperation, 1)
BENCHMARK_PARAM(MultiThreadedStatOperation, 4)
BENCHMARK_PARAM(MultiThreadedStatOperation, 16)
BENCHMARK_PARAM(MultiThreadedStatOperation, 64)
BENCHMARK_PARAM(MultiThreadedHistogramOperation, 1)
BENCHMARK_PARAM(MultiThreadedHistogramOperation, 4)
BENCHMARK_PARAM(MultiThreadedHistogramOperation, 16)
BENCHMARK_PARAM(MultiThreadedHistogramOperation, 64)

int main(int argc, char** argv) {
  folly::Init init{&argc, &argv, true};
  runBenchmarks();
  return 0;
}
