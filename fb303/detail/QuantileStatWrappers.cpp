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

#include <fb303/ServiceData.h>
#include <fb303/detail/QuantileStatWrappers.h>

namespace facebook::fb303::detail {

QuantileStatWrapper::QuantileStatWrapper(
    folly::StringPiece name,
    folly::Range<const ExportType*> stats,
    folly::Range<const double*> quantiles,
    folly::Range<const size_t*> slidingWindowPeriods)
    : stat_(ServiceData::get()->getQuantileStat(
          name,
          stats,
          quantiles,
          slidingWindowPeriods)) {}

QuantileStatWrapper::QuantileStatWrapper(
    folly::StringPiece /*unused*/,
    folly::StringPiece name,
    folly::Range<const ExportType*> stats,
    folly::Range<const double*> quantiles,
    folly::Range<const size_t*> slidingWindowPeriods)
    : QuantileStatWrapper(name, stats, quantiles, slidingWindowPeriods) {}

void QuantileStatWrapper::addValue(
    double value,
    std::chrono::steady_clock::time_point now) {
  stat_->addValue(value, now);
}

} // namespace facebook::fb303::detail
