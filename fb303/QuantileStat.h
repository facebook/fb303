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

#include <atomic>
#include <chrono>
#include <cstddef>

#include <folly/stats/QuantileEstimator.h>
#include <folly/stats/TDigest.h>

namespace facebook::fb303 {

template <typename ClockT>
class BasicQuantileStat {
 public:
  using TimePoint = typename ClockT::time_point;

  explicit BasicQuantileStat(
      const std::vector<std::pair<std::chrono::seconds, size_t>>& defs);

  void addValue(double value, TimePoint now = ClockT::now());

  struct SlidingWindowEstimate {
    SlidingWindowEstimate() = delete;
    SlidingWindowEstimate(
        folly::QuantileEstimates&& e,
        std::chrono::seconds wl,
        size_t n)
        : estimate(std::move(e)), windowLength(wl), nWindows(n) {}

    folly::QuantileEstimates estimate;
    std::chrono::seconds windowLength;
    size_t nWindows;

    std::chrono::seconds slidingWindowLength() const {
      return windowLength * nWindows;
    }
  };

  struct Estimates {
    folly::QuantileEstimates allTimeEstimate;
    std::vector<SlidingWindowEstimate> slidingWindows;
  };

  Estimates getEstimates(
      folly::Range<const double*> quantiles,
      TimePoint now = ClockT::now());

  std::vector<std::chrono::seconds> getSlidingWindowLengths() const;

  TimePoint creationTime() const;

  // QuantileEstimator buffers values added to it for buffer duration.
  // This method can be force buffer flush and digest rebuild.
  void flush();

  struct SlidingWindowSnapshot {
    folly::TDigest digest;
    std::chrono::seconds windowLength;
    size_t nWindows;

    std::chrono::seconds slidingWindowLength() const {
      return windowLength * nWindows;
    }
  };

  struct Snapshot {
    folly::TDigest allTimeDigest;
    std::vector<SlidingWindowSnapshot> slidingWindowSnapshot;
    typename ClockT::time_point now;
    typename ClockT::time_point creationTime;
  };

  Snapshot getSnapshot(TimePoint now = ClockT::now());

 private:
  struct SlidingWindow {
    SlidingWindow(std::chrono::seconds wl, size_t n)
        : windowLength(wl), nWindows(n) {}

    std::chrono::seconds windowLength;
    size_t nWindows;

    std::chrono::seconds slidingWindowLength() const {
      return windowLength * nWindows;
    }
  };

  std::vector<SlidingWindow> slidingWindowVec_;
  folly::MultiSlidingWindowQuantileEstimator<ClockT> estimator_;
  const TimePoint creationTime_;
};

using QuantileStat = BasicQuantileStat<std::chrono::steady_clock>;

extern template class BasicQuantileStat<std::chrono::steady_clock>;

} // namespace facebook::fb303

#include <fb303/QuantileStat-inl.h>
