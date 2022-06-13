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

#include <common/fb303/cpp/DefaultMonitor.h>
#include <fb303/BaseService.h>
#include <fb303/test/gen-cpp2/TestService.h>
#include <thrift/lib/cpp2/util/ScopedServerInterfaceThread.h>
#include <chrono>

#include <gtest/gtest.h>

using namespace facebook::fb303;

class TestHandlerBaseService : public TestServiceSvIf, public BaseService {
 public:
  TestHandlerBaseService() : BaseService("TestService") {
    auto* dynamicCounters = fbData->getDynamicCounters();
    dynamicCounters->registerCallback("counterA", [] { return 1; });
    dynamicCounters->registerCallback("counterB", [] { return 0; });
    dynamicCounters->registerCallback("counterC", [] { return 0; });
  }

  cpp2::fb303_status getStatus() override {
    return cpp2::fb303_status::ALIVE;
  }
};

// old way of using BaseService is getting deprecated
class TestHandlerDefaultMonitor : public facebook::fb303::DefaultMonitor {
 public:
  TestHandlerDefaultMonitor() : DefaultMonitor() {
    auto* dynamicCounters = fbData->getDynamicCounters();
    dynamicCounters->registerCallback("counterA", [] { return 1; });
    dynamicCounters->registerCallback("counterB", [] { return 0; });
    dynamicCounters->registerCallback("counterC", [] { return 0; });
  }
};

template <typename T>
class GetCountersWithLimitTest : public testing::Test {
 protected:
  std::string fb303CountersLimit{kCountersLimitHeader};
  std::string fb303CountersAvailable{kCountersAvailableHeader};
  std::unique_ptr<facebook::fb303::TestServiceAsyncClient> fb303Client;
  std::shared_ptr<T> handler;
  std::shared_ptr<apache::thrift::ScopedServerInterfaceThread> server;

  void HandlerSetup() {
    handler = std::make_shared<T>();
    server =
        std::make_shared<apache::thrift::ScopedServerInterfaceThread>(handler);
    fb303Client = server->newClient<facebook::fb303::TestServiceAsyncClient>();
  }
};

using MyTypes =
    ::testing::Types<TestHandlerBaseService, TestHandlerDefaultMonitor>;
TYPED_TEST_SUITE(GetCountersWithLimitTest, MyTypes);

TYPED_TEST(GetCountersWithLimitTest, getCountersWithNoLimit) {
  this->HandlerSetup();
  auto opt = apache::thrift::RpcOptions();
  opt.setTimeout(std::chrono::seconds(5));

  std::map<std::string, int64_t> counters;

  /////////////////////////////////////////////////////////
  // First read the counters with no header
  this->fb303Client->sync_getCounters(opt, counters);
  EXPECT_EQ(counters.size(), 3);
  auto it = opt.getReadHeaders().find(this->fb303CountersAvailable);
  EXPECT_TRUE(it == opt.getReadHeaders().end());
}

TYPED_TEST(GetCountersWithLimitTest, getCountersWithLimit1) {
  this->HandlerSetup();
  auto opt = apache::thrift::RpcOptions();
  opt.setTimeout(std::chrono::seconds(5));

  std::map<std::string, int64_t> counters;
  /////////////////////////////////////////////////////////
  // Now set the header limit to 1 and read
  opt.setWriteHeader(this->fb303CountersLimit, std::to_string(1));
  this->fb303Client->sync_getCounters(opt, counters);
  EXPECT_EQ(counters.size(), 1);
  EXPECT_EQ("counterA", counters.begin()->first);
  auto it = opt.getReadHeaders().find(this->fb303CountersAvailable);
  EXPECT_TRUE(it != opt.getReadHeaders().end());
  EXPECT_EQ(it->second, "3");
}
/////////////////////////////////////////////////////////
// Now set the header limit to 3 and read
TYPED_TEST(GetCountersWithLimitTest, getCountersWithLimit3) {
  this->HandlerSetup();
  auto opt = apache::thrift::RpcOptions();
  opt.setTimeout(std::chrono::seconds(5));

  std::map<std::string, int64_t> counters;
  opt.setWriteHeader(this->fb303CountersLimit, std::to_string(3));
  this->fb303Client->sync_getCounters(opt, counters);
  EXPECT_EQ(counters.size(), 3);
  auto it = opt.getReadHeaders().find(this->fb303CountersAvailable);
  EXPECT_TRUE(it != opt.getReadHeaders().end());
  EXPECT_EQ(it->second, "3");
}

/////////////////////////////////////////////////////////
// Now set the header limit to -20 and read. Negative
// number imply unlimited.
TYPED_TEST(GetCountersWithLimitTest, getCountersWithLimitNeg) {
  this->HandlerSetup();
  auto opt = apache::thrift::RpcOptions();
  opt.setTimeout(std::chrono::seconds(5));

  std::map<std::string, int64_t> counters;
  opt.setWriteHeader(this->fb303CountersLimit, std::to_string(-20));
  this->fb303Client->sync_getCounters(opt, counters);
  EXPECT_EQ(counters.size(), 3);
  auto it = opt.getReadHeaders().find(this->fb303CountersAvailable);
  EXPECT_TRUE(it == opt.getReadHeaders().end());
}

/////////////////////////////////////////////////////////
// Clear the limit to 0
TYPED_TEST(GetCountersWithLimitTest, getCountersWithLimitZero) {
  this->HandlerSetup();
  auto opt = apache::thrift::RpcOptions();
  opt.setTimeout(std::chrono::seconds(5));

  std::map<std::string, int64_t> counters;
  opt.setWriteHeader(this->fb303CountersLimit, std::to_string(0));
  this->fb303Client->sync_getCounters(opt, counters);
  EXPECT_EQ(counters.size(), 0);
  auto it = opt.getReadHeaders().find(this->fb303CountersAvailable);
  EXPECT_TRUE(it != opt.getReadHeaders().end());
  EXPECT_EQ(it->second, "3");
}

/////////////////////////////////////////////////////////
// Set the limit to 100
TYPED_TEST(GetCountersWithLimitTest, getCountersWithLimitHundred) {
  this->HandlerSetup();
  auto opt = apache::thrift::RpcOptions();
  opt.setTimeout(std::chrono::seconds(5));

  std::map<std::string, int64_t> counters;
  opt.setWriteHeader(this->fb303CountersLimit, std::to_string(100));
  this->fb303Client->sync_getCounters(opt, counters);
  EXPECT_EQ(counters.size(), 3);
  auto it = opt.getReadHeaders().find(this->fb303CountersAvailable);
  EXPECT_TRUE(it != opt.getReadHeaders().end());
  EXPECT_EQ(it->second, "3");
}

TYPED_TEST(GetCountersWithLimitTest, getCountersWithRegexLimit1) {
  this->HandlerSetup();
  auto opt = apache::thrift::RpcOptions();
  opt.setTimeout(std::chrono::seconds(5));

  std::map<std::string, int64_t> counters;
  /////////////////////////////////////////////////////////
  // Now set the header limit to 1 and read
  opt.setWriteHeader(this->fb303CountersLimit, std::to_string(1));
  this->fb303Client->sync_getRegexCounters(opt, counters, "counter.*");
  EXPECT_EQ(counters.size(), 1);
  EXPECT_EQ("counterA", counters.begin()->first);
  auto it = opt.getReadHeaders().find(this->fb303CountersAvailable);
  EXPECT_TRUE(it != opt.getReadHeaders().end());
  EXPECT_EQ(it->second, "3");
}

TYPED_TEST(GetCountersWithLimitTest, getCountersWithRegexLimit0) {
  this->HandlerSetup();
  auto opt = apache::thrift::RpcOptions();
  opt.setTimeout(std::chrono::seconds(5));

  std::map<std::string, int64_t> counters;
  /////////////////////////////////////////////////////////
  // Now set the header limit to 0 and read
  opt.setWriteHeader(this->fb303CountersLimit, std::to_string(0));
  this->fb303Client->sync_getRegexCounters(opt, counters, "counterA.*");
  EXPECT_EQ(counters.size(), 0);
  auto it = opt.getReadHeaders().find(this->fb303CountersAvailable);
  EXPECT_TRUE(it != opt.getReadHeaders().end());
  EXPECT_EQ(it->second, "1"); // expect 1 counter to be matched on server side
}

TYPED_TEST(GetCountersWithLimitTest, getCountersWithRegexLimit) {
  this->HandlerSetup();
  auto opt = apache::thrift::RpcOptions();
  opt.setTimeout(std::chrono::seconds(5));

  std::map<std::string, int64_t> counters;
  /////////////////////////////////////////////////////////
  // Now set the header limit to 3 and read
  opt.setWriteHeader(this->fb303CountersLimit, std::to_string(3));
  this->fb303Client->sync_getRegexCounters(opt, counters, "counterA.*");
  EXPECT_EQ(counters.size(), 1);
  auto it = opt.getReadHeaders().find(this->fb303CountersAvailable);
  EXPECT_TRUE(it != opt.getReadHeaders().end());
  EXPECT_EQ(it->second, "1"); // expect 1 counter to be matched on server side
}
