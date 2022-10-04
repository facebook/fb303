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
#include <thrift/lib/cpp2/util/ScopedServerInterfaceThread.h>

#include <folly/Conv.h>
#include <folly/Random.h>

#include <fb303/BaseService.h>
#include <fb303/test/gen-cpp2/TestService.h>

using namespace facebook::fb303;

using folly::Random;
using std::condition_variable;
using std::mutex;
using std::string;
using std::thread;
using std::unique_lock;
using std::vector;

/* This test case creates a fb303 server and async client
 * The server has regex key caching enabled.
 * Hence it is able to look at previous patterns and send
 *  the counters whose keys match the pattern
 */
class TestHandler : public TestServiceSvIf, public BaseService {
  static constexpr int kMaxIter = 3000;

 public:
  TestHandler() : BaseService("TestService") {
    auto* dynamicCounters = fbData->getDynamicCounters();
    for (int iter = 0; iter < kMaxIter; iter++) {
      auto counterName = folly::to<string>("matchingCounter", iter);
      dynamicCounters->registerCallback(counterName, [iter]() { return iter; });
    }
  }

  /* This will clear the cache and so subsequent calls would be unoptimized*/
  void clearAndAddCounter() {
    auto* dynamicCounters = fbData->getDynamicCounters();
    dynamicCounters->unregisterCallback("counter");
    dynamicCounters->registerCallback("counter", [] { return 0; });
  }

  /* This just adds a counter */
  void addCounter(const string& counterName, int iter) {
    auto* dynamicCounters = fbData->getDynamicCounters();
    dynamicCounters->registerCallback(counterName, [iter] { return iter; });
  }

  cpp2::fb303_status getStatus() override {
    return cpp2::fb303_status::ALIVE;
  }
};

namespace {

constexpr int kFixedThreads = 3;
constexpr int kBumpThreads = 29;
constexpr int kNumPerThread = 1000;

mutex gMtx;
condition_variable gCond;
int gPhase = -1;

void phaseAdvance() {
  unique_lock<mutex> lock(gMtx);
  ++gPhase;
  gCond.notify_all();
}

void phaseAwait(int phase) {
  unique_lock<mutex> lock(gMtx);
  while (gPhase < phase) {
    gCond.wait(lock);
  }
}

// This is a test to ensure caching is thread safe,
// we have 2 read threads calling getRegexCounters using a thrift client
// We also create 2 write threads,
// that periodically invalidates the cache by clearing and adding new counters
template <typename F1, typename F2, typename F3>
void runInThreads(F1 f1, F2 f2, F3 f3, std::shared_ptr<TestHandler> handler) {
  vector<thread> threads(kFixedThreads + kBumpThreads);
  threads[0] = thread(f1);
  threads[1] = thread(f2);
  threads[2] = thread(f3);
  for (int i = 0; i < kBumpThreads; ++i) {
    threads[i + kFixedThreads] = thread([&handler, i]() {
      phaseAwait(0);
      int randNum = 5000 + (Random::rand32() % 5001);
      for (int j = i * kNumPerThread; j < ((i + 1) * kNumPerThread); ++j) {
        auto counterName = folly::to<string>("counter_add_", j);
        handler->addCounter(counterName, j);
        usleep(randNum);
      }
      phaseAdvance();
    });
  }
  phaseAdvance(); // tell the threads to begin
  for (auto& th : threads) {
    th.join();
  }
}

} // namespace

TEST(GetRegexCountersCachedTest, GetRegexCountersOptimizedMultithreaded) {
  auto handler = std::make_shared<TestHandler>();
  apache::thrift::ScopedServerInterfaceThread server(handler);
  auto const address = server.getAddress();

  auto opt = apache::thrift::RpcOptions();
  opt.setTimeout(std::chrono::seconds(10));

  auto fb303Client1 = server.newClient<TestServiceAsyncClient>();
  auto fb303Client2 = server.newClient<TestServiceAsyncClient>();

  runInThreads(
      [&fb303Client1, &opt]() {
        phaseAwait(0);
        for (int i = 0; i < 60; ++i) {
          std::map<string, int64_t> counters;
          fb303Client1->sync_getRegexCounters(opt, counters, "matching.*");

          for (auto& [key, value] : counters) {
            auto counterName = folly::to<string>("matchingCounter", value);
            ASSERT_EQ(key, counterName);
          }
        }
      },
      [&fb303Client2, &opt]() {
        phaseAwait(0);
        for (int i = 0; i < 50; ++i) {
          std::map<string, int64_t> counters;
          fb303Client2->sync_getRegexCounters(opt, counters, "counter.*");
        }
        phaseAwait(kBumpThreads);
        std::map<string, int64_t> counters;
        fb303Client2->sync_getRegexCounters(opt, counters, "counter.*");
        for (auto& [key, value] : counters) {
          if (key == "counter") {
            continue;
          }
          auto counterName = folly::to<string>("counter_add_", value);
          ASSERT_EQ(key, counterName);
        }
        ASSERT_EQ(counters.size(), kNumPerThread * kBumpThreads + 1);
      },
      [&handler]() {
        phaseAwait(0);
        int randNum = 5000 + (Random::rand32() % 5001);
        for (int i = 0; i < 1000; ++i) {
          handler->clearAndAddCounter();
          usleep(randNum);
        }
      },
      handler);
}
