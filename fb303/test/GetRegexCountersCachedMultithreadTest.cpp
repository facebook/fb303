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

#include <chrono>
#include <thread>

#include <gtest/gtest.h>

#include <folly/Conv.h>
#include <folly/Memory.h>
#include <folly/Random.h>
#include <folly/Try.h>
#include <folly/executors/CPUThreadPoolExecutor.h>
#include <thrift/lib/cpp2/async/AsyncProcessor.h>
#include <thrift/lib/cpp2/util/ScopedServerInterfaceThread.h>

#include <fb303/ServiceData.h>
#include <fb303/thrift/gen-cpp2/BaseService.h>

using namespace facebook::fb303;
using namespace facebook::fb303::cpp2;

using folly::Random;
using std::condition_variable;
using std::mutex;
using std::string;
using std::thread;
using std::unique_lock;

/* This test case creates a fb303 server and async client
 * The server has regex key caching enabled.
 * Hence it is able to look at previous patterns and send
 *  the counters whose keys match the pattern
 */
class TestHandler : virtual public BaseServiceSvIf {
  ServiceData& data_;

 public:
  explicit TestHandler(ServiceData& data) : data_{data} {}

  // the use of thread='eb' means this is the only option to override
  void async_eb_getRegexCounters(
      std::unique_ptr<apache::thrift::HandlerCallback<
          std::unique_ptr<std::map<std::string, int64_t>>>> callback,
      std::unique_ptr<std::string> regex) override {
    getCountersExecutor_.add(
        [this, callback = std::move(callback), regex = std::move(regex)] {
          callback->complete(folly::makeTryWith([&] {
            return folly::copy_to_unique_ptr(data_.getRegexCounters(*regex));
          }));
        });
  }

  folly::CPUThreadPoolExecutor getCountersExecutor_{16}; // BaseService has 2
};

namespace {

class Phased {
 private:
  mutex mtx_;
  condition_variable cond_;
  int phase_ = -1;

 public:
  void advance() {
    unique_lock<mutex> lock(mtx_);
    ++phase_;
    cond_.notify_all();
  }

  void await(int target) {
    unique_lock<mutex> lock(mtx_);
    while (phase_ < target) {
      cond_.wait(lock);
    }
  }
};

} // namespace

// This is a test to ensure caching is thread safe,
// we have 2 read threads calling getRegexCounters using a thrift client
// We also create 2 write threads,
// that periodically invalidates the cache by clearing and adding new counters
TEST(GetRegexCountersCachedTest, GetRegexCountersOptimizedMultithreaded) {
  constexpr int kMaxIter = 3000;
  constexpr int kFixedThreads = 3;
  constexpr int kBumpThreads = 29;
  constexpr int kNumPerThread = 1000;

  ServiceData data;
  auto& dyn = *data.getDynamicCounters();
  for (int iter = 0; iter < kMaxIter; iter++) {
    auto counterName = folly::to<string>("matchingCounter", iter);
    dyn.registerCallback(counterName, [iter] { return iter; });
  }

  auto handler = std::make_shared<TestHandler>(data);
  apache::thrift::ScopedServerInterfaceThread server(handler);

  Phased phase;
  std::vector<thread> threads;
  threads.emplace_back([&server, &phase] {
    phase.await(0);
    auto client = server.newClient<BaseServiceAsyncClient>();
    for (int i = 0; i < 60; ++i) {
      std::map<string, int64_t> counters;
      client->sync_getRegexCounters(counters, "matching.*");

      for (auto& [key, value] : counters) {
        auto counterName = folly::to<string>("matchingCounter", value);
        ASSERT_EQ(key, counterName);
      }
    }
  });
  threads.emplace_back([&server, &phase] {
    phase.await(0);
    auto client = server.newClient<BaseServiceAsyncClient>();
    for (int i = 0; i < 50; ++i) {
      std::map<string, int64_t> counters;
      client->sync_getRegexCounters(counters, "counter.*");
    }
    phase.await(kBumpThreads);
    std::map<string, int64_t> counters;
    client->sync_getRegexCounters(counters, "counter.*");
    for (auto& [key, value] : counters) {
      if (key == "counter") {
        continue;
      }
      auto counterName = folly::to<string>("counter_add_", value);
      ASSERT_EQ(key, counterName);
    }
    ASSERT_EQ(counters.size(), kNumPerThread * kBumpThreads + 1);
  });
  threads.emplace_back([&phase, &dyn] {
    phase.await(0);
    for (int i = 0; i < 1000; ++i) {
      dyn.unregisterCallback("counter"); // bust the cache
      dyn.registerCallback("counter", [] { return 0; });
      /* sleep override */ std::this_thread::sleep_for(
          std::chrono::microseconds(Random::rand32(5000, 10001)));
    }
  });
  for (int i = 0; i < kBumpThreads; ++i) {
    threads.emplace_back([&phase, &dyn, i] {
      phase.await(0);
      for (int j = 0; j < kNumPerThread; ++j) {
        int k = i * kNumPerThread + j;
        auto counterName = folly::to<string>("counter_add_", k);
        dyn.registerCallback(counterName, [k] { return k; });
        /* sleep override */ std::this_thread::sleep_for(
            std::chrono::microseconds(Random::rand32(5000, 10001)));
      }
      phase.advance();
    });
  }
  phase.advance(); // tell the threads to begin
  for (auto& th : threads) {
    th.join();
  }
}
