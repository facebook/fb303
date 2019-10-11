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

#include <fb303/ExportedStatMapImpl.h>
#include <folly/String.h>
#include "common/stats/ServiceData.h"

#include <gtest/gtest.h>
#include <atomic>
#include <thread>

using namespace std;
using namespace facebook;
using namespace facebook::stats;

namespace {

const std::string kCounterName = "generic.counter.name";
std::atomic<int> currentSuffix(0);
const int kMaxSuffix = 1024 * 32;
const int kMaxThreads = 4;

void addNewCounters() {
  int rounds = 0;
  while (rounds < kMaxSuffix) {
    while (currentSuffix > rounds) {
      fbData->addStatValue(folly::to<std::string>(kCounterName, '.', rounds));
      ++rounds;
    }
  }

  LOG(INFO) << "rounds for thread " << pthread_self() << " is " << rounds;
}

} // namespace

void testExportedNewCounters() {
  // start multiple threads
  // put them into wait state
  // wake up at once and let them output compound counters
  std::vector<std::thread> addThreads;
  for (int n = 0; n < kMaxThreads; ++n) {
    addThreads.emplace_back(addNewCounters);
  }

  for (; currentSuffix != kMaxSuffix; ++currentSuffix) {
    usleep(1);
  }

  for (auto& thread : addThreads) {
    thread.join();
  }

  // export counters
  std::map<std::string, int64_t> counters;
  fbData->getCounters(counters);
}

TEST(ExportedStatMapImplLoad, MultithreadedExport) {
  testExportedNewCounters();
}
