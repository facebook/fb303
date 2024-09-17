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

#pragma once

#include <array>
#include <atomic>
#include <chrono>
#include <memory>

#include <fb303/ExportType.h>
#include <fb303/QuantileStat.h>
#include <fb303/ThreadCachedServiceData.h>
#include <folly/Range.h>
#include <folly/json/dynamic.h>

namespace facebook::fb303::detail {

class QuantileStatWrapper {
 public:
  explicit QuantileStatWrapper(
      folly::StringPiece name,
      folly::Range<const ExportType*> stats = ExportTypeConsts::kCountAvg,
      folly::Range<const double*> quantiles = QuantileConsts::kP95_P99_P999,
      folly::Range<const size_t*> slidingWindowPeriods =
          SlidingWindowPeriodConsts::kOneMin);
  QuantileStatWrapper(
      folly::StringPiece /*unused*/,
      folly::StringPiece name,
      folly::Range<const ExportType*> stats = ExportTypeConsts::kCountAvg,
      folly::Range<const double*> quantiles = QuantileConsts::kP95_P99_P999,
      folly::Range<const size_t*> slidingWindowPeriods =
          SlidingWindowPeriodConsts::kOneMin);

  void addValue(
      double value,
      std::chrono::steady_clock::time_point now =
          std::chrono::steady_clock::now());

 private:
  using QuantileStat = BasicQuantileStat<std::chrono::steady_clock>;
  std::shared_ptr<QuantileStat> stat_;
};

template <size_t N>
class DynamicQuantileStatWrapper {
 public:
  explicit DynamicQuantileStatWrapper(
      std::string keyFormat,
      folly::Range<const ExportType*> stats = ExportTypeConsts::kCountAvg,
      folly::Range<const double*> quantiles = QuantileConsts::kP95_P99_P999,
      folly::Range<const size_t*> timeseriesLengths =
          SlidingWindowPeriodConsts::kOneMin);

  template <typename... Args>
  void addValue(
      double value,
      std::chrono::steady_clock::time_point now,
      Args&&... subkeys);

  template <typename... Args>
  void addValue(double value, Args&&... subkeys);

 private:
  struct Spec {
    std::vector<ExportType> stats;
    std::vector<double> quantiles;
    std::vector<size_t> timeseriesLengths;
  };
  using StatPtr = std::shared_ptr<QuantileStat>;
  using Cache = folly::F14FastMap<std::string_view, StatPtr>;

  internal::FormattedKeyHolder<N, StatPtr> key_;
  folly::ThreadLocal<Cache> cache_;
  Spec spec_;
};

} // namespace facebook::fb303::detail

#include <fb303/detail/QuantileStatWrappers-inl.h>

/*
 * Stats are registered and exported via ServiceData. These macros allow you to
 * declare and define specific stat instances.
 *
 * Example usage:
 *
 * using namespace facebook::fb303;
 * DEFINE_quantile_stat(
 *    my_stat,
 *    ExportTypeConsts::kAvg,
 *    QuantileConsts::kP95_P99,
 *    SlidingWindowPeriodConsts::kOneMinTenMinHour);
 *
 * void foo() {
 *   STATS_my_stat.addValue(1);
 * }
 *
 * In this example, we are registering a stat with the name "my_stat", which is
 * tracking aggregations for average (mean), p95, and p99, across minute, ten
 * minute, and hour long sliding windows. If your binary is a thrift service,
 * the aggregated counters are exported via fb303::getCounters. Otherwise, you
 * can access them through the global ServiceData object (see ServiceData::get).
 */

#define DECLARE_quantile_stat(varname) \
  extern ::facebook::fb303::detail::QuantileStatWrapper STATS_##varname

#define DEFINE_quantile_stat(varname, ...)                        \
  ::facebook::fb303::detail::QuantileStatWrapper STATS_##varname( \
      #varname, ##__VA_ARGS__)

#define DECLARE_dynamic_quantile_stat(varname, keyNumArgs)                 \
  extern ::facebook::fb303::detail::DynamicQuantileStatWrapper<keyNumArgs> \
      STATS_##varname

#define DEFINE_dynamic_quantile_stat(varname, keyformat, ...)            \
  static_assert(                                                         \
      countPlaceholders(keyformat) > 0,                                  \
      "Must have at least one placeholder.");                            \
  facebook::fb303::detail::DynamicQuantileStatWrapper<countPlaceholders( \
      keyformat)>                                                        \
      STATS_##varname(keyformat, ##__VA_ARGS__)
