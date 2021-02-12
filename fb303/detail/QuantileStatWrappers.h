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
#include <atomic>
#include <chrono>
#include <memory>

#include <fb303/ExportType.h>
#include <fb303/QuantileStat.h>
#include <folly/Range.h>
#include <folly/dynamic.h>

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

  virtual ~DynamicQuantileStatWrapper() {
    delete stats_.load(std::memory_order_relaxed);
  }

  template <typename... Args>
  void addValue(
      double value,
      std::chrono::steady_clock::time_point now,
      Args&&... subkeys);

  template <typename... Args>
  void addValue(double value, Args&&... subkeys);

 private:
  using SubkeyArray = std::array<folly::dynamic, N>;

  class SubkeyArrayHash {
   public:
    size_t operator()(const SubkeyArray& v) const {
      size_t res = 0;
      for (const auto& s : v) {
        res ^= s.hash();
      }
      return res;
    }
  };

  struct Spec {
    std::vector<ExportType> stats;
    std::vector<double> quantiles;
    std::vector<size_t> timeseriesLengths;
  };

  std::string format_;
  Spec spec_;

  class MapHolder;
  std::atomic<MapHolder*> stats_;
};

} // namespace facebook::fb303::detail

#include <fb303/detail/QuantileStatWrappers-inl.h>
