/*
 * Copyright 2019-present Facebook, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include <fb303/TimeseriesExporter.h>
#include <gtest/gtest.h>

using namespace facebook::fb303;
using namespace folly::string_piece_literals;

namespace {

constexpr size_t kNameBufferSize = 100;

struct TimeseriesExporterTest : ::testing::Test {
  using ExportedStatT = MinuteTenMinuteHourTimeSeries<CounterType>;

  char name[kNameBufferSize];
  const ExportedStatT stat;
};

} // namespace

#define EXPECT_COUNTER_NAME(                                           \
    expected, stat_name, buffer_size, export_type, level)              \
  do {                                                                 \
    static_assert(buffer_size <= kNameBufferSize);                     \
    TimeseriesExporter::getCounterName(                                \
        name, buffer_size, &stat, stat_name##_sp, export_type, level); \
    EXPECT_EQ(expected, std::string{name});                            \
  } while (false)

TEST_F(TimeseriesExporterTest, getCounterName) {
  EXPECT_COUNTER_NAME(
      "stat_name.sum.60",
      "stat_name",
      20,
      ExportType::SUM,
      ExportedStatT::MINUTE);
  EXPECT_COUNTER_NAME(
      "stat_name.sum",
      "stat_name",
      20,
      ExportType::SUM,
      ExportedStatT::ALLTIME);
  EXPECT_COUNTER_NAME(
      "stat_name.count.60",
      "stat_name",
      20,
      ExportType::COUNT,
      ExportedStatT::MINUTE);
  EXPECT_COUNTER_NAME(
      "stat_name.avg.60",
      "stat_name",
      20,
      ExportType::AVG,
      ExportedStatT::MINUTE);
  EXPECT_COUNTER_NAME(
      "stat_name.rate.60",
      "stat_name",
      20,
      ExportType::RATE,
      ExportedStatT::MINUTE);
  EXPECT_COUNTER_NAME(
      "stat_name.pct.60",
      "stat_name",
      20,
      ExportType::PERCENT,
      ExportedStatT::MINUTE);
}

TEST_F(TimeseriesExporterTest, getCounterName_always_zero_terminates) {
  // If the buffer wasn't null-terminated, the construction of
  // std::string from the name buffer would read too far.

  EXPECT_COUNTER_NAME(
      "stat_name", "stat_name", 10, ExportType::SUM, ExportedStatT::MINUTE);
  EXPECT_COUNTER_NAME(
      "stat_name.", "stat_name", 11, ExportType::SUM, ExportedStatT::MINUTE);
  EXPECT_COUNTER_NAME(
      "stat_name.sum.6",
      "stat_name",
      16,
      ExportType::SUM,
      ExportedStatT::MINUTE);
  EXPECT_COUNTER_NAME(
      "stat_name.sum.60",
      "stat_name",
      17,
      ExportType::SUM,
      ExportedStatT::MINUTE);

  EXPECT_COUNTER_NAME(
      "stat_name", "stat_name", 10, ExportType::SUM, ExportedStatT::ALLTIME);
  EXPECT_COUNTER_NAME(
      "stat_name.", "stat_name", 11, ExportType::SUM, ExportedStatT::ALLTIME);
  EXPECT_COUNTER_NAME(
      "stat_name.su", "stat_name", 13, ExportType::SUM, ExportedStatT::ALLTIME);
  EXPECT_COUNTER_NAME(
      "stat_name.sum",
      "stat_name",
      14,
      ExportType::SUM,
      ExportedStatT::ALLTIME);
}
