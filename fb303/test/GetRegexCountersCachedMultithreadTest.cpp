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

#include <boost/regex.hpp>
#include <fb303/BaseService.h>
#include <fb303/test/gen-cpp2/TestService.h>
#include <folly/DynamicConverter.h>
#include <folly/Optional.h>
#include <folly/Random.h>
#include <folly/String.h>
#include <folly/init/Init.h>
#include <gtest/gtest.h>
#include <thrift/lib/cpp2/util/ScopedServerInterfaceThread.h>
#include <chrono>
#include <functional>

#include <time.h>
using namespace std;
using namespace facebook::fb303;
using namespace folly;

/* This test case creates a fb303 server and async client
 * The server has regex key caching enabled.
 * Hence it is able to look at previous patterns and send
 *  the counters whose keys match the pattern
 */
class TestHandler : public TestServiceSvIf, public BaseService {
  const int kMaxIter = 3000;

 public:
  static const int kGetRegexCountersIter = 100;
  long iter;
  TestHandler() : BaseService("TestService") {
    auto* dynamicCounters = fbData->getDynamicCounters();
    for (iter = 0; iter < kMaxIter; iter++) {
      auto counterName =
          "matchingCounter" + folly::convertTo<std::string>(iter);
      int iterCopy = iter;
      dynamicCounters->registerCallback(
          counterName, [iterCopy]() { return iterCopy; });
    }
  }

  /* This will clear the cache and so subsequent calls would be unoptimized*/
  void clearAndAddCounter() {
    auto* dynamicCounters = fbData->getDynamicCounters();
    dynamicCounters->unregisterCallback("counter");
    dynamicCounters->registerCallback("counter", [] { return 0; });
  }

  /* This just adds a counter */
  void addCounter(const std::string& counterName, int& iter) {
    auto* dynamicCounters = fbData->getDynamicCounters();
    dynamicCounters->registerCallback(counterName, [iter] { return iter; });
  }

  cpp2::fb303_status getStatus() override {
    return cpp2::fb303_status::ALIVE;
  }
};

// This is a test to ensure caching is thread safe,
// we have 2 read threads calling getRegexCounters using a thrift client
// We also create 2 write threads,
// that periodically invalidates the cache by clearing and adding new counters
template <typename F1, typename F2, typename F3>
static void
runInThreads(F1 f1, F2 f2, F3 f3, std::shared_ptr<TestHandler> handler) {
  vector<thread> threads(32);
  threads[0] = thread(f1);
  threads[1] = thread(f2);
  threads[2] = thread(f3);
  for (int i = 3; i < 32; i++) {
    threads[i] = thread([&handler, i]() {
      int i1 = (i - 2) * 1000;
      int randNum = 5000 + (folly::Random::rand32() % 5001);
      while (i1 > (i - 3) * 1000) {
        i1--;
        auto counterName = "counter_add_" + folly::convertTo<std::string>(i1);
        handler->addCounter(counterName, i1);
        usleep(randNum);
      }
    });
  }
  for (auto& th : threads) {
    th.join();
  }
}

TEST(GetRegexCountersCachedTest, GetRegexCountersOptimizedMultithreaded) {
  auto handler = std::make_shared<TestHandler>();
  apache::thrift::ScopedServerInterfaceThread server(handler);
  auto const address = server.getAddress();

  auto opt = apache::thrift::RpcOptions();
  opt.setTimeout(std::chrono::seconds(10));

  auto fb303Client1 =
      server.newClient<facebook::fb303::TestServiceAsyncClient>();
  auto fb303Client2 =
      server.newClient<facebook::fb303::TestServiceAsyncClient>();

  runInThreads(
      [=, &fb303Client1, &opt]() {
        int i = 60;
        while (i--) {
          std::map<std::string, int64_t> counters;
          fb303Client1->sync_getRegexCounters(opt, counters, "matching.*");

          for (auto& [key, value] : counters) {
            auto counterName =
                "matchingCounter" + folly::convertTo<std::string>(value);
            ASSERT_EQ(key, counterName);
          }
        }
      },
      [=, &fb303Client2, &opt]() {
        int i = 50;
        while (i--) {
          std::map<std::string, int64_t> counters;
          fb303Client2->sync_getRegexCounters(opt, counters, "counter.*");
        }
        std::map<std::string, int64_t> counters;
        fb303Client2->sync_getRegexCounters(opt, counters, "counter.*");
        for (auto& [key, value] : counters) {
          auto counterName =
              "counter_add_" + folly::convertTo<std::string>(value);
          if (key == "counter")
            continue;
          ASSERT_EQ(key, counterName);
        }
        ASSERT_EQ(counters.size(), 29001);
      },
      [=, &handler]() {
        int i = 1000;
        int randNum = 5000 + (folly::Random::rand32() % 5001);
        while (i--) {
          handler->clearAndAddCounter();
          usleep(randNum);
        }
      },
      handler);
}
