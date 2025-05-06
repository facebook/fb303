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

namespace facebook {
namespace fb303 {

template <typename ClockT>
BasicQuantileStat<ClockT>::BasicQuantileStat(
    const std::vector<std::pair<std::chrono::seconds, size_t>>& defs)
    : estimator_(defs), creationTime_(ClockT::now()) {
  for (const auto& def : defs) {
    slidingWindowVec_.emplace_back(def.first, def.second);
  }
}

template <typename ClockT>
void BasicQuantileStat<ClockT>::addValue(double value, TimePoint now) {
  estimator_.addValue(value, now);
}

template <typename ClockT>
void BasicQuantileStat<ClockT>::flush() {
  estimator_.flush();
}

template <typename ClockT>
typename BasicQuantileStat<ClockT>::Estimates
BasicQuantileStat<ClockT>::getEstimates(
    folly::Range<const double*> quantiles,
    TimePoint now) {
  auto estimates = estimator_.estimateQuantiles(quantiles, now);

  Estimates result;
  result.allTimeEstimate = std::move(estimates.allTime);
  result.slidingWindows.reserve(slidingWindowVec_.size());
  for (size_t i = 0; i < slidingWindowVec_.size(); ++i) {
    result.slidingWindows.emplace_back(
        std::move(estimates.windows[i]),
        slidingWindowVec_[i].windowLength,
        slidingWindowVec_[i].nWindows);
  }
  return result;
}

template <typename ClockT>
std::vector<std::chrono::seconds>
BasicQuantileStat<ClockT>::getSlidingWindowLengths() const {
  std::vector<std::chrono::seconds> windowLengths;
  windowLengths.reserve(slidingWindowVec_.size());
  for (const auto& slidingWindow : slidingWindowVec_) {
    windowLengths.push_back(slidingWindow.slidingWindowLength());
  }
  return windowLengths;
}

template <typename ClockT>
typename BasicQuantileStat<ClockT>::TimePoint
BasicQuantileStat<ClockT>::creationTime() const {
  return creationTime_;
}

template <typename ClockT>
typename BasicQuantileStat<ClockT>::Snapshot
BasicQuantileStat<ClockT>::getSnapshot(TimePoint now) {
  auto digests = estimator_.getDigests(now);

  Snapshot snapshot;
  snapshot.now = now;
  snapshot.creationTime = creationTime_;
  snapshot.allTimeDigest = std::move(digests.allTime);

  snapshot.slidingWindowSnapshot.reserve(slidingWindowVec_.size());
  for (size_t i = 0; i < slidingWindowVec_.size(); ++i) {
    auto& snap = snapshot.slidingWindowSnapshot.emplace_back();
    snap.windowLength = slidingWindowVec_[i].windowLength;
    snap.nWindows = slidingWindowVec_[i].nWindows;
    snap.digest = std::move(digests.windows[i]);
  }
  return snapshot;
}

} // namespace fb303
} // namespace facebook
