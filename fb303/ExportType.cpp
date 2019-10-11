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

#include <fb303/ExportType.h>

namespace facebook {
namespace fb303 {

constexpr const std::size_t ExportTypeMeta::kNumExportTypes;
constexpr const std::array<ExportType, ExportTypeMeta::kNumExportTypes>
    ExportTypeMeta::kExportTypes;

constexpr const std::array<ExportType, 0> ExportTypeConsts::kNone;
constexpr const std::array<ExportType, 1> ExportTypeConsts::kSum;
constexpr const std::array<ExportType, 1> ExportTypeConsts::kCount;
constexpr const std::array<ExportType, 1> ExportTypeConsts::kAvg;
constexpr const std::array<ExportType, 1> ExportTypeConsts::kRate;
constexpr const std::array<ExportType, 2> ExportTypeConsts::kSumCount;
constexpr const std::array<ExportType, 2> ExportTypeConsts::kSumAvg;
constexpr const std::array<ExportType, 2> ExportTypeConsts::kSumRate;
constexpr const std::array<ExportType, 2> ExportTypeConsts::kCountAvg;
constexpr const std::array<ExportType, 2> ExportTypeConsts::kCountRate;
constexpr const std::array<ExportType, 2> ExportTypeConsts::kAvgRate;
constexpr const std::array<ExportType, 3> ExportTypeConsts::kSumCountAvg;
constexpr const std::array<ExportType, 3> ExportTypeConsts::kSumCountRate;
constexpr const std::array<ExportType, 3> ExportTypeConsts::kSumAvgRate;
constexpr const std::array<ExportType, 3> ExportTypeConsts::kCountAvgRate;
constexpr const std::array<ExportType, 4> ExportTypeConsts::kSumCountAvgRate;

constexpr const std::array<double, 3> QuantileConsts::kP50_P95_P99;
constexpr const std::array<double, 4> QuantileConsts::kP50_P90_P95_P99;
constexpr const std::array<double, 1> QuantileConsts::kP95;
constexpr const std::array<double, 1> QuantileConsts::kP99;
constexpr const std::array<double, 1> QuantileConsts::kP999;
constexpr const std::array<double, 2> QuantileConsts::kP95_P99;
constexpr const std::array<double, 2> QuantileConsts::kP95_P999;
constexpr const std::array<double, 2> QuantileConsts::kP99_P999;
constexpr const std::array<double, 2> QuantileConsts::kP99_P100;
constexpr const std::array<double, 3> QuantileConsts::kP95_P99_P999;
constexpr const std::array<double, 3> QuantileConsts::kP95_P99_P100;
constexpr const std::array<double, 4> QuantileConsts::kP5_P50_P95_P99;
constexpr const std::array<double, 4> QuantileConsts::kP50_P75_P95_P99;
constexpr const std::array<double, 4> QuantileConsts::kP10_P50_P90_P99;
constexpr const std::array<double, 4> QuantileConsts::kP10_P50_P95_P99;
constexpr const std::array<double, 5> QuantileConsts::kP5_P50_P95_P99_P100;
constexpr const std::array<double, 5> QuantileConsts::kP10_P75_P95_P99_P100;
constexpr const std::array<double, 5> QuantileConsts::kP50_P75_P90_P95_P99;
constexpr const std::array<double, 5> QuantileConsts::kP1_P10_P50_P90_P99;
constexpr const std::array<double, 6> QuantileConsts::kP50_P75_P90_P95_P99_P999;

constexpr const std::array<size_t, 1> SlidingWindowPeriodConsts::kOneMin;
constexpr const std::array<size_t, 2> SlidingWindowPeriodConsts::kOneMinTenMin;
constexpr const std::array<size_t, 3>
    SlidingWindowPeriodConsts::kOneMinTenMinHour;
} // namespace fb303
} // namespace facebook
