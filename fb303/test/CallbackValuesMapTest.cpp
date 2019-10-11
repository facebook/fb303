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

#include <fb303/CallbackValuesMap.h>

#include <mutex>
#include <thread>

#include <boost/bind.hpp>
#include <gtest/gtest.h>

using boost::bind;
using facebook::fb303::CallbackValuesMap;
using std::string;

typedef CallbackValuesMap<int> TestCallbackValuesMap;

// simple callback function for testing
static int echo(const int value) {
  return value;
}

// ------------------------------------------------------------
// TEST: CallbackValuesMapBasic
// ------------------------------------------------------------

TEST(CallbackValuesMapTest, CallbackValuesMapBasic) {
  TestCallbackValuesMap map;
  const string key1 = "key1";
  const string key2 = "key2";
  // Test empty map
  EXPECT_FALSE(map.contains(key1));
  EXPECT_FALSE(map.contains(key2));
  int value = -1;
  TestCallbackValuesMap::ValuesMap values;
  EXPECT_FALSE(map.getValue(key1, &value));
  EXPECT_FALSE(map.getValue(key2, &value));
  EXPECT_EQ(-1, value);
  map.getValues(&values);
  EXPECT_TRUE(values.empty());

  // Add some keys
  map.registerCallback(key1, bind(echo, 123));
  map.registerCallback(key2, bind(echo, 321));
  EXPECT_TRUE(map.contains(key1));
  EXPECT_TRUE(map.contains(key2));
  EXPECT_TRUE(map.getValue(key1, &value));
  EXPECT_EQ(123, value);
  EXPECT_TRUE(map.getValue(key2, &value));
  EXPECT_EQ(321, value);
  map.getValues(&values);
  EXPECT_EQ(2, values.size());
  EXPECT_EQ(123, values[key1]);
  EXPECT_EQ(321, values[key2]);

  value = -1;
  values.clear();
  // remove some keys
  EXPECT_TRUE(map.unregisterCallback(key1));
  EXPECT_FALSE(map.contains(key1));
  EXPECT_FALSE(map.getValue(key1, &value));
  EXPECT_EQ(-1, value);
  map.getValues(&values);
  EXPECT_EQ(1, values.size());
  EXPECT_EQ(321, values[key2]);
  EXPECT_FALSE(map.unregisterCallback(key1));
}

TEST(CallbackValuesMapTest, PossibleDeadlock) {
  // see t660896 for more details
  std::mutex m;
  m.lock();
  TestCallbackValuesMap callbackMap;
  callbackMap.registerCallback("a", [&m]() {
    m.lock();
    return 0;
  });
  std::thread bg([&callbackMap]() {
    std::map<std::string, int> values;
    callbackMap.getValues(&values);
  }); // this will block for m
  sleep(1);
  callbackMap.registerCallback("b", []() { return 1; });
  m.unlock();
  bg.join();
  SUCCEED();
}
