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

/*

This benchmark tries to simulate an intensified version of realistic use.
It focuses on the getRegexCounters() variant and not getCounters().
Specifically, that means there are multiple threads exercising the internal
locks.  Typical applications do many more counter updates than anything
else.  Next most common is responding to requests for counters/values.
Least common is changing the set of counters (adding or removing one).

The benchmark also keeps a count of some easily-identifiable errors.
This is to keep us honest, not to substitute for an actual test.

Internally, the things that can typically make an fb303 implementation of
getRegexCounters() slow are: lock contention preventing full CPU
utilization; and regular expression processing, specifically matching.

Results (median of three)
20-core (40-thread) Intel(R) Xeon(R) Gold 6138 CPU @ 2.00GHz

============================================================================
fb303/test/GetRegexCountersMtBench.cpp         relative  time/iter   iters/s
============================================================================
multithreaded                                               26.58s    37.62m

*/

#include <atomic>
#include <ostream>
#include <string>
#include <thread>
#include <vector>

#include <folly/Benchmark.h>
#include <folly/Conv.h>
#include <folly/Random.h>
#include <folly/init/Init.h>

#include <fb303/ServiceData.h>

using facebook::fb303::ServiceData;
using folly::Random;
using std::atomic;
using std::string;
using std::thread;
using std::vector;

DEFINE_uint32(threads, 32, "Number of threads");
DEFINE_uint32(batch, 32, "Number of operations per batch");
DEFINE_uint32(batches, 1024, "Number of batches per run");
DEFINE_uint32(counters, 20000, "Number of unique counters");
DEFINE_string(prefix, "sso_spills_over_counter_", "Prefix of counter names");
DEFINE_bool(opt, true, "Use getRegexCountersOptimized()");

namespace {

// These are adapted from actual observed workloads...
const string kRegex0 =
    "(?!(bad.*$)|(.*unwanted$)|(.*terrible.*$)|(.*counter_[0-9]_from_hell$)|(.*worst.*$))((good.*)|(.*better.*)|(.*best)|(.*counter_[0-9]*0)|(.*desired.*))";
const string kRegex1 =
    "(?!(bad.*$)|(.*unwanted$)|(.*terrible.*$)|(.*counter_[0-9]_from_hell$)|(.*worst.*$))((good.*)|(.*better.*)|(.*best)|(.*counter_[0-9]*1)|(.*desired.*))";

string counterName(uint32_t x) {
  return folly::to<string>(FLAGS_prefix, x);
}

void setCounter(ServiceData& serviceData, uint32_t x) {
  serviceData.setCounter(counterName(x), x);
}

void prePopulate(ServiceData& serviceData) {
  for (uint32_t i = 0; i < FLAGS_counters; ++i) {
    setCounter(serviceData, i);
  }
}

void doWork(
    ServiceData& serviceData,
    atomic<uint32_t>& batchCount,
    atomic<uint32_t>& errorCount) {
  uint32_t errors = 0;
  uint32_t batches = 0;
  while (batches < FLAGS_batches) {
    for (uint32_t i = 0; i < FLAGS_batch; ++i) {
      uint32_t dice = Random::rand32(100);
      // 1% delete a counter (invalidate cache)
      if (dice < 1) {
        uint32_t r = Random::rand32(FLAGS_counters);
        serviceData.clearCounter(counterName(r));
      }
      // 20% get counters by regex and check them
      else if (dice < 21) {
        const string& regex = (dice & 1) ? kRegex1 : kRegex0;
        std::map<string, int64_t> got;
        if (FLAGS_opt) {
          serviceData.getRegexCountersOptimized(got, regex);
        } else {
          serviceData.getRegexCounters(got, regex);
        }
        if (got.size() == 0) {
          ++errors;
        } else {
          for (const auto& [key, val] : got) {
            if (key != folly::to<string>(FLAGS_prefix, val)) {
              ++errors;
            }
          }
        }
      }
      // 79% set a counter
      else {
        uint32_t r = Random::rand32(FLAGS_counters);
        setCounter(serviceData, r);
      }
    }
    batches = batchCount.fetch_add(1, std::memory_order_relaxed);
  }
  errorCount.fetch_add(errors, std::memory_order_relaxed);
}

} // namespace

BENCHMARK(multithreaded) {
  folly::BenchmarkSuspender suspender;
  ServiceData serviceData;
  atomic<uint32_t> batchCount = 0;
  atomic<uint32_t> errorCount = 0;
  prePopulate(serviceData);
  vector<thread> threads;
  threads.reserve(FLAGS_threads);
  suspender.dismiss();

  for (int i = 0; i < FLAGS_threads; ++i) {
    threads.emplace_back(
        doWork,
        std::ref(serviceData),
        std::ref(batchCount),
        std::ref(errorCount));
  }
  for (thread& thr : threads) {
    thr.join();
  }

  suspender.rehire();
  CHECK_EQ(0, errorCount);
}

int main(int argc, char** argv) {
  folly::Init init(&argc, &argv, true);
  folly::runBenchmarks();
  return 0;
}
