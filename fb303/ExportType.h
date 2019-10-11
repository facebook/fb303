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

#pragma once

#include <array>

#include <folly/container/Array.h>

namespace facebook {
namespace fb303 {

// There is no support for a MAX value type. Reasonable workarounds I've seen:
// 1. Maintain your own maximum and export it as a raw counter
// 2. Use a histogram and export one of the higher percentiles, like p99, p999,
//    or p100. See QuantileConsts below.
enum ExportType : int {
  SUM,
  COUNT,
  AVG,
  RATE,
  PERCENT,
};

struct ExportTypeMeta {
  static constexpr const auto kExportTypes = folly::make_array(
      ExportType::SUM,
      ExportType::COUNT,
      ExportType::AVG,
      ExportType::RATE,
      ExportType::PERCENT);
  static constexpr const std::size_t kNumExportTypes = kExportTypes.size();
};

struct ExportTypeConsts {
  static constexpr const std::array<ExportType, 0> kNone{{}};
  static constexpr const std::array<ExportType, 1> kSum{{ExportType::SUM}};
  static constexpr const std::array<ExportType, 1> kCount{{ExportType::COUNT}};
  static constexpr const std::array<ExportType, 1> kAvg{{ExportType::AVG}};
  static constexpr const std::array<ExportType, 1> kRate{{ExportType::RATE}};
  static constexpr const std::array<ExportType, 2> kSumCount{
      {ExportType::SUM, ExportType::COUNT}};
  static constexpr const std::array<ExportType, 2> kSumAvg{
      {ExportType::SUM, ExportType::AVG}};
  static constexpr const std::array<ExportType, 2> kSumRate{
      {ExportType::SUM, ExportType::RATE}};
  static constexpr const std::array<ExportType, 2> kCountAvg{
      {ExportType::COUNT, ExportType::AVG}};
  static constexpr const std::array<ExportType, 2> kCountRate{
      {ExportType::COUNT, ExportType::RATE}};
  static constexpr const std::array<ExportType, 2> kAvgRate{
      {ExportType::AVG, ExportType::RATE}};
  static constexpr const std::array<ExportType, 3> kSumCountAvg{
      {ExportType::SUM, ExportType::COUNT, ExportType::AVG}};
  static constexpr const std::array<ExportType, 3> kSumCountRate{
      {ExportType::SUM, ExportType::COUNT, ExportType::RATE}};
  static constexpr const std::array<ExportType, 3> kSumAvgRate{
      {ExportType::SUM, ExportType::AVG, ExportType::RATE}};
  static constexpr const std::array<ExportType, 3> kCountAvgRate{
      {ExportType::COUNT, ExportType::AVG, ExportType::RATE}};
  static constexpr const std::array<ExportType, 4> kSumCountAvgRate{
      {ExportType::SUM, ExportType::COUNT, ExportType::AVG, ExportType::RATE}};
};

struct QuantileConsts {
  static constexpr const std::array<double, 3> kP50_P95_P99{{.5, .95, .99}};
  static constexpr const std::array<double, 4> kP50_P90_P95_P99{
      {.5, .9, .95, .99}};
  static constexpr const std::array<double, 1> kP95{{.95}};
  static constexpr const std::array<double, 1> kP99{{.99}};
  static constexpr const std::array<double, 1> kP999{{.999}};
  static constexpr const std::array<double, 2> kP95_P99{{.95, .99}};
  static constexpr const std::array<double, 2> kP95_P999{{.95, .999}};
  static constexpr const std::array<double, 2> kP99_P999{{.99, .999}};
  static constexpr const std::array<double, 2> kP99_P100{{.99, 1}};
  static constexpr const std::array<double, 3> kP95_P99_P999{{.95, .99, .999}};
  static constexpr const std::array<double, 3> kP95_P99_P100{{.95, .99, 1}};
  static constexpr const std::array<double, 4> kP5_P50_P95_P99{
      {.05, .5, .95, .99}};
  static constexpr const std::array<double, 4> kP50_P75_P95_P99{
      {.5, .75, .95, .99}};
  static constexpr const std::array<double, 4> kP10_P50_P90_P99{
      {.1, .5, .9, .99}};
  static constexpr const std::array<double, 4> kP10_P50_P95_P99{
      {.1, .5, .95, .99}};
  static constexpr const std::array<double, 5> kP5_P50_P95_P99_P100{
      {.05, .5, .95, .99, 1}};
  static constexpr const std::array<double, 5> kP10_P75_P95_P99_P100{
      {.1, .75, .95, .99, 1}};
  static constexpr const std::array<double, 5> kP50_P75_P90_P95_P99{
      {.5, .75, .90, .95, .99}};
  static constexpr const std::array<double, 5> kP1_P10_P50_P90_P99{
      {.01, .1, .5, .9, .99}};
  static constexpr const std::array<double, 6> kP50_P75_P90_P95_P99_P999{
      {.5, .75, .9, .95, .99, .999}};
};

struct SlidingWindowPeriodConsts {
  static constexpr const std::array<size_t, 1> kOneMin{{60}};
  static constexpr const std::array<size_t, 2> kOneMinTenMin{{60, 600}};
  static constexpr const std::array<size_t, 3> kOneMinTenMinHour{
      {60, 600, 3600}};
};

} // namespace fb303
} // namespace facebook
