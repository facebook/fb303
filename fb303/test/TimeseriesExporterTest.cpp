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

#include <fb303/TimeseriesExporter.h>
#include <gtest/gtest.h>

using namespace facebook::fb303;

namespace {

class TimeseriesExporterTest : public testing::Test {
 public:
  using ExportedStatT = MinuteTenMinuteHourTimeSeries<CounterType>;

  std::string getCounterNameChecked(
      char const* stat_name,
      size_t buffer_size,
      ExportType export_type,
      ExportedStatT::Levels level) {
    static constexpr size_t kNameBufferSize = 100;

    if (buffer_size > kNameBufferSize) {
      throw std::invalid_argument("buffer_size > kNameBufferSize");
    }

    char name[kNameBufferSize];
    const ExportedStatT stat;
    TimeseriesExporter::getCounterName(
        name, buffer_size, &stat, stat_name, export_type, level);
    return std::string{name};
  }
};

} // namespace

TEST_F(TimeseriesExporterTest, getCounterName) {
  EXPECT_EQ(
      "stat_name.sum.60",
      getCounterNameChecked(
          "stat_name", 20, ExportType::SUM, ExportedStatT::MINUTE));
  EXPECT_EQ(
      "stat_name.sum",
      getCounterNameChecked(
          "stat_name", 20, ExportType::SUM, ExportedStatT::ALLTIME));
  EXPECT_EQ(
      "stat_name.count.60",
      getCounterNameChecked(
          "stat_name", 20, ExportType::COUNT, ExportedStatT::MINUTE));
  EXPECT_EQ(
      "stat_name.avg.60",
      getCounterNameChecked(
          "stat_name", 20, ExportType::AVG, ExportedStatT::MINUTE));
  EXPECT_EQ(
      "stat_name.rate.60",
      getCounterNameChecked(
          "stat_name", 20, ExportType::RATE, ExportedStatT::MINUTE));
  EXPECT_EQ(
      "stat_name.pct.60",
      getCounterNameChecked(
          "stat_name", 20, ExportType::PERCENT, ExportedStatT::MINUTE));
}

TEST_F(TimeseriesExporterTest, getCounterName_always_zero_terminates) {
  // If the buffer wasn't null-terminated, the construction of
  // std::string from the name buffer would read too far.

  EXPECT_EQ(
      "stat_name",
      getCounterNameChecked(
          "stat_name", 10, ExportType::SUM, ExportedStatT::MINUTE));
  EXPECT_EQ(
      "stat_name.",
      getCounterNameChecked(
          "stat_name", 11, ExportType::SUM, ExportedStatT::MINUTE));
  EXPECT_EQ(
      "stat_name.sum.6",
      getCounterNameChecked(
          "stat_name", 16, ExportType::SUM, ExportedStatT::MINUTE));
  EXPECT_EQ(
      "stat_name.sum.60",
      getCounterNameChecked(
          "stat_name", 17, ExportType::SUM, ExportedStatT::MINUTE));

  EXPECT_EQ(
      "stat_name",
      getCounterNameChecked(
          "stat_name", 10, ExportType::SUM, ExportedStatT::ALLTIME));
  EXPECT_EQ(
      "stat_name.",
      getCounterNameChecked(
          "stat_name", 11, ExportType::SUM, ExportedStatT::ALLTIME));
  EXPECT_EQ(
      "stat_name.su",
      getCounterNameChecked(
          "stat_name", 13, ExportType::SUM, ExportedStatT::ALLTIME));
  EXPECT_EQ(
      "stat_name.sum",
      getCounterNameChecked(
          "stat_name", 14, ExportType::SUM, ExportedStatT::ALLTIME));
}
