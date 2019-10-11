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

#include <fb303/LegacyClock.h>

#include <gtest/gtest.h>
#include <chrono>

using namespace std::chrono;
using facebook::fb303::get_legacy_stats_time;

namespace {
time_t get_system_clock_now() {
  return system_clock::to_time_t(system_clock::now());
}

time_t absdiff(time_t x, time_t y) {
  return (x > y) ? (x - y) : (y - x);
}
} // namespace

TEST(LegacyClockTest, legacy_clock_within_second_of_system_clock) {
  static constexpr time_t kEpsilon = 1; // seconds
  static constexpr int kIterations = 1000;

  time_t minimumDifference =
      absdiff(get_system_clock_now(), get_legacy_stats_time());
  for (int i = 1; i < kIterations; ++i) {
    minimumDifference = std::min(
        minimumDifference,
        absdiff(get_system_clock_now(), get_legacy_stats_time()));
  }

  EXPECT_LE(minimumDifference, kEpsilon);
}

TEST(LegacyClockTest, system_seconds_clock_within_second_of_std_time) {
  static constexpr time_t kEpsilon = 1; // seconds
  static constexpr int kIterations = 1000;

  time_t minimumDifference =
      absdiff(std::time(nullptr), get_legacy_stats_time());
  for (int i = 1; i < kIterations; ++i) {
    minimumDifference = std::min(
        minimumDifference,
        absdiff(std::time(nullptr), get_legacy_stats_time()));
  }

  EXPECT_LE(minimumDifference, kEpsilon);
}
