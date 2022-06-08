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
    std::vector<BasicQuantileStat<ClockT>::SlidingWindow> defs)
    : slidingWindowVec_(std::move(defs)), creationTime_(ClockT::now()) {}

template <typename ClockT>
BasicQuantileStat<ClockT>::BasicQuantileStat(
    const std::vector<std::pair<std::chrono::seconds, size_t>>& defs)
    : creationTime_(ClockT::now()) {
  for (const auto& def : defs) {
    slidingWindowVec_.emplace_back(def.first, def.second);
  }
}

template <typename ClockT>
void BasicQuantileStat<ClockT>::addValue(double value, TimePoint now) {
  allTimeEstimator_.addValue(value, now);
  for (auto& slidingWindow : slidingWindowVec_) {
    slidingWindow.estimator.addValue(value, now);
  }
}

template <typename ClockT>
void BasicQuantileStat<ClockT>::flush() {
  allTimeEstimator_.flush();
  for (auto& slidingWindow : slidingWindowVec_) {
    slidingWindow.estimator.flush();
  }
}

template <typename ClockT>
typename BasicQuantileStat<ClockT>::Estimates
BasicQuantileStat<ClockT>::getEstimates(
    folly::Range<const double*> quantiles,
    TimePoint now) {
  Estimates estimates;

  estimates.allTimeEstimate =
      allTimeEstimator_.estimateQuantiles(quantiles, now);

  estimates.slidingWindows.reserve(slidingWindowVec_.size());
  for (auto& slidingWindow : slidingWindowVec_) {
    estimates.slidingWindows.emplace_back(
        slidingWindow.estimator.estimateQuantiles(quantiles, now),
        slidingWindow.windowLength,
        slidingWindow.nWindows);
  }
  return estimates;
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
  Snapshot snapshot;
  snapshot.now = now;
  snapshot.creationTime = creationTime_;
  snapshot.allTimeDigest = allTimeEstimator_.getDigest(now);

  snapshot.slidingWindowSnapshot.reserve(slidingWindowVec_.size());
  for (auto& slidingWindow : slidingWindowVec_) {
    SlidingWindowSnapshot snap;
    snap.windowLength = slidingWindow.windowLength;
    snap.nWindows = slidingWindow.nWindows;
    snap.digest = slidingWindow.estimator.getDigest(now);
    snapshot.slidingWindowSnapshot.push_back(std::move(snap));
  }
  return snapshot;
}

} // namespace fb303
} // namespace facebook
