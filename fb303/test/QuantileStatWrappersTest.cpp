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

#include <fb303/detail/QuantileStatWrappers.h>

#include <cstdint>
#include <map>
#include <string>

#include <fb303/ServiceData.h>

#include <gtest/gtest.h>

using namespace facebook::fb303;
using namespace facebook::fb303::detail;

namespace {

constexpr std::array<double, 1> kP100{{1.0}};

// Populates `n` distinct subkeys, then re-touches each with a distinct larger
// value. The re-touch is what exercises DynamicQuantileStatWrapper::
// getStatEntry's local-cache lookup: every entry already exists, so each
// call must hit the cache and route to the right Entry rather than falling
// through to the slow path and silently masking a broken lookup.
void populateAndRetouch(DynamicQuantileStatWrapper<1>& wrapper, size_t n) {
  auto const now = std::chrono::steady_clock::now();
  for (size_t i = 0; i < n; ++i) {
    wrapper.addValue(100 + i, now, std::to_string(i));
  }
  for (size_t i = 0; i < n; ++i) {
    wrapper.addValue(1000 + i, now, std::to_string(i));
  }
}

// Asserts that `keyPrefix.<i>.p100` (and its ".60" sliding-window sibling)
// exists for each of `n` subkeys and holds the larger, second-pass value,
// i.e. that the re-touch in populateAndRetouch() landed on the same Entry as
// the first touch. Comparing one map of key to value in a single EXPECT_EQ
// checks both key presence and value correctness together.
void expectRetouchedValues(const std::string& keyPrefix, size_t n) {
  ServiceData::get()->flushAllData();

  std::map<std::string, int64_t> expected;
  for (size_t i = 0; i < n; ++i) {
    auto const key = keyPrefix + "." + std::to_string(i) + ".p100";
    expected[key] = 1000 + i;
    expected[key + ".60"] = 1000 + i;
  }

  std::map<std::string, int64_t> actual;
  for (auto const& key : ServiceData::get()->getCounterKeys()) {
    if (key.rfind(keyPrefix + ".", 0) == 0) {
      actual[key] = ServiceData::get()->getCounter(key);
    }
  }

  EXPECT_EQ(expected, actual);
}

} // namespace

class QuantileStatWrappersTest : public ::testing::Test {};

TEST_F(QuantileStatWrappersTest, LinearScanBelowThreshold) {
  auto const n = DynamicQuantileStatWrapper<1>::kLocalCacheLinearScanThreshold;
  DynamicQuantileStatWrapper<1> wrapper(
      "linscan.{}", ExportTypeConsts::kNone, kP100);
  populateAndRetouch(wrapper, n);
  expectRetouchedValues("linscan", n);
}

TEST_F(QuantileStatWrappersTest, HashLookupAboveThreshold) {
  auto const n =
      DynamicQuantileStatWrapper<1>::kLocalCacheLinearScanThreshold + 4;
  DynamicQuantileStatWrapper<1> wrapper(
      "hashfind.{}", ExportTypeConsts::kNone, kP100);
  populateAndRetouch(wrapper, n);
  expectRetouchedValues("hashfind", n);
}
