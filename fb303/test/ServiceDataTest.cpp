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

#include "common/stats/ServiceData.h"

#include <gflags/gflags.h>
#include <gtest/gtest.h>

using namespace std;
using namespace testing;
using namespace facebook::stats;

class ServiceDataTest : public Test {
 public:
  ServiceData data;
};

TEST_F(ServiceDataTest, getCounters_rvo_example) {
  data.setCounter("wiggle", 6);
  auto expected = map<string, int64_t>{{"wiggle", 6}};
  EXPECT_EQ(expected, data.getCounters());
}

TEST_F(ServiceDataTest, getSelectedCounters_rvo_example) {
  data.setCounter("wiggle", 6);
  data.setCounter("strike", 8);
  auto expected = map<string, int64_t>{{"wiggle", 6}};
  EXPECT_EQ(expected, data.getSelectedCounters({"wiggle"}));
}

TEST_F(ServiceDataTest, getRegexCounters_rvo_example) {
  data.setCounter("wiggle", 6);
  data.setCounter("strike", 8);
  auto expected = map<string, int64_t>{{"wiggle", 6}};
  EXPECT_EQ(expected, data.getRegexCounters("w.+"));
}

TEST_F(ServiceDataTest, getExportedValue_rvo_example) {
  data.setExportedValue("wiggle", "6");
  auto expected = "6";
  EXPECT_EQ(expected, data.getExportedValue("wiggle"));
}

TEST_F(ServiceDataTest, getExportedValues_rvo_example) {
  data.setExportedValue("wiggle", "6");
  auto expected = map<string, string>{{"wiggle", "6"}};
  EXPECT_EQ(expected, data.getExportedValues());
}

TEST_F(ServiceDataTest, getSelectedExportedValues_rvo_example) {
  data.setExportedValue("wiggle", "6");
  data.setExportedValue("strike", "8");
  auto expected = map<string, string>{{"wiggle", "6"}};
  EXPECT_EQ(expected, data.getSelectedExportedValues({"wiggle"}));
}

TEST_F(ServiceDataTest, getRegexExportedValues_rvo_example) {
  data.setExportedValue("wiggle", "6");
  data.setExportedValue("strike", "8");
  auto expected = map<string, string>{{"wiggle", "6"}};
  EXPECT_EQ(expected, data.getRegexExportedValues("w.+"));
}

TEST_F(ServiceDataTest, registerQuantile) {
  auto foo = data.getQuantileStat("foo");
  auto alsofoo = data.getQuantileStat("foo");
  EXPECT_EQ(foo, alsofoo);

  auto keys = data.getCounterKeys();
  EXPECT_EQ(10, keys.size());
  EXPECT_TRUE(data.hasCounter("foo.avg"));
  EXPECT_TRUE(data.hasCounter("foo.avg.60"));
  EXPECT_TRUE(data.hasCounter("foo.count"));
  EXPECT_TRUE(data.hasCounter("foo.count.60"));
  EXPECT_TRUE(data.hasCounter("foo.p95"));
  EXPECT_TRUE(data.hasCounter("foo.p95.60"));
  EXPECT_TRUE(data.hasCounter("foo.p99"));
  EXPECT_TRUE(data.hasCounter("foo.p99.60"));
  EXPECT_TRUE(data.hasCounter("foo.p99.9"));
  EXPECT_TRUE(data.hasCounter("foo.p99.9.60"));
  EXPECT_FALSE(data.hasCounter("notfoo.avg"));

  keys = {"foo.avg", "foo.avg.60", "foo.p99.9.60"};
  std::map<std::string, int64_t> counters;

  data.getSelectedCounters(counters, keys);
  EXPECT_EQ(3, counters.size());

  counters.clear();

  data.getCounters(counters);
  EXPECT_EQ(10, counters.size());

  auto notfoo = data.getQuantileStat("notfoo");
  EXPECT_NE(foo, notfoo);
  EXPECT_TRUE(data.hasCounter("notfoo.avg"));
}

TEST_F(ServiceDataTest, clearCounter) {
  auto const key = "key";

  data.clearCounter(key);
  EXPECT_TRUE(data.getCounters().empty());

  data.setCounter(key, 12);
  EXPECT_FALSE(data.getCounters().empty());

  data.clearCounter(key);
  EXPECT_TRUE(data.getCounters().empty());
}

TEST_F(ServiceDataTest, allowedFlags) {
  auto getflags = []() -> std::map<std::string, std::string> {
    std::map<std::string, std::string> _return;
    vector<gflags::CommandLineFlagInfo> allFlags;
    gflags::GetAllFlags(&allFlags);
    for (const auto& entry : allFlags) {
      _return[entry.name] = entry.current_value;
    }
    return _return;
  };

  data.setUseOptionsAsFlags(false);

  data.setOption("vmodule", "foobar=foobar");
  data.setOption("logmailer", "foobar");
  data.setOption("alsologtoemail", "foobar");
  auto ret = getflags();
  EXPECT_EQ(ret["vmodule"], "foobar=foobar");
  EXPECT_NE(ret["logmailer"], "foobar");
  EXPECT_NE(ret["alsologtoemail"], "foobar");

  auto oldWhitelistflags = ret["whitelist_flags"];

  data.setUseOptionsAsFlags(true);

  data.setOption("vmodule", "foobar2=foobar2");
  data.setOption("logmailer", "foobar2");
  data.setOption("alsologtoemail", "foobar2");
  data.setOption("whitelist_flags", "*");
  ret = getflags();
  EXPECT_EQ(ret["vmodule"], "foobar2=foobar2");
  EXPECT_EQ(ret["alsologtoemail"], "foobar2");
  EXPECT_NE(ret["logmailer"], "foobar2");
  EXPECT_NE(ret["whitelist_flags"], "*");
  EXPECT_EQ(ret["whitelist_flags"], oldWhitelistflags);
}
