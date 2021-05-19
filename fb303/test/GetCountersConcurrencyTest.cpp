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

#include <fb303/BaseService.h>
#include <fb303/test/gen-cpp2/TestService.h>
#include <thrift/lib/cpp2/util/ScopedServerInterfaceThread.h>
#include <chrono>

#include <gtest/gtest.h>

using namespace facebook::fb303;

class TestHandler : public TestServiceSvIf, public BaseService {
 public:
  TestHandler() : BaseService("TestService") {
    auto* dynamicCounters = fbData->getDynamicCounters();
    dynamicCounters->registerCallback("burnCounter", [this] {
      if (burnGetCounters) {
        burnTime(1);
      }
      return 0;
    });
  }

  cpp2::fb303_status getStatus() override {
    return cpp2::fb303_status::ALIVE;
  }

  void burnTime(int32_t seconds) override {
    {
      std::lock_guard g(burnMutex);
      burnStarted++;
      burnCondition.notify_all();
    }

    auto begin = std::chrono::steady_clock::now();

    while (!burnStopped) {
      auto end = std::chrono::steady_clock::now();
      if (end - begin >= std::chrono::seconds(seconds)) {
        break;
      }
    }
    burnStarted--;
    burnCondition.notify_all();
  }

  void waitForBurning(size_t target = 1) {
    std::unique_lock g(burnMutex);
    if (burnStarted != target) {
      burnCondition.wait(g);
    }
  }

  void stopBurning() {
    burnStopped = true;
  }

  void setBurnGetCounters(bool burn) {
    burnGetCounters = burn;
  }

 private:
  std::mutex burnMutex;
  std::condition_variable burnCondition;
  std::atomic<size_t> burnStarted{0};
  std::atomic<bool> burnStopped{false};
  std::atomic<bool> burnGetCounters{false};
};

class GetCountersConcurrencyTest : public testing::Test {};

TEST_F(GetCountersConcurrencyTest, concurrentGetCounters) {
  auto handler = std::make_shared<TestHandler>();
  apache::thrift::ScopedServerInterfaceThread server(handler);
  auto const address = server.getAddress();

  auto client = server.newClient<facebook::fb303::TestServiceAsyncClient>();
  auto opt = apache::thrift::RpcOptions();
  opt.setTimeout(std::chrono::seconds(1));
  std::map<std::string, int64_t> counters;

  auto burnTimeClient =
      server.newClient<facebook::fb303::TestServiceAsyncClient>();

  burnTimeClient->semifuture_burnTime(3);
  SCOPE_EXIT {
    handler->stopBurning();
  };

  // Ensure burning has started
  handler->waitForBurning();

  client->sync_getCounters(opt, counters);
  client->sync_getRegexCounters(opt, counters, ".");
}

TEST_F(GetCountersConcurrencyTest, concurrentGetCountersBurnCounters) {
  auto handler = std::make_shared<TestHandler>();
  handler->setGetCountersExpiration(std::chrono::milliseconds(500));
  handler->setBurnGetCounters(true);
  apache::thrift::ScopedServerInterfaceThread server(handler);
  auto const address = server.getAddress();

  auto client = server.newClient<facebook::fb303::TestServiceAsyncClient>();
  auto opt = apache::thrift::RpcOptions();
  opt.setTimeout(std::chrono::seconds(3));
  std::map<std::string, int64_t> counters;

  auto burnTimeClient =
      server.newClient<facebook::fb303::TestServiceAsyncClient>();

  SCOPE_EXIT {
    handler->stopBurning();
  };

  // One for each getCountersExecutor thread
  burnTimeClient->semifuture_getCounters();
  burnTimeClient->semifuture_getCounters();

  // Ensure burning has started
  handler->waitForBurning(2);

  // These calls will time out in the getCountersExecutor queue, and don't burn
  EXPECT_THROW(
      client->sync_getCounters(opt, counters),
      apache::thrift::TApplicationException);
  handler->waitForBurning(0);

  // One for each getCountersExecutor thread
  burnTimeClient->semifuture_getCounters();
  burnTimeClient->semifuture_getCounters();

  // Ensure burning has started
  handler->waitForBurning(2);

  EXPECT_THROW(
      client->sync_getRegexCounters(opt, counters, "."),
      apache::thrift::TApplicationException);
}
