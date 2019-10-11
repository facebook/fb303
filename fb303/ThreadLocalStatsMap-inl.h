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

namespace facebook {
namespace fb303 {

template <class LockTraits>
ThreadLocalStatsMapT<LockTraits>::ThreadLocalStatsMapT(ServiceData* serviceData)
    : ThreadLocalStatsT<LockTraits>(serviceData) {}

template <class LockTraits>
void ThreadLocalStatsMapT<LockTraits>::addStatValue(
    folly::StringPiece name,
    int64_t value) {
  auto state = state_.lock();
  getTimeseriesLocked(*state, name)->addValue(value);
}

template <class LockTraits>
void ThreadLocalStatsMapT<LockTraits>::addStatValueAggregated(
    folly::StringPiece name,
    int64_t sum,
    int64_t numSamples) {
  auto state = state_.lock();
  getTimeseriesLocked(*state, name)->addValueAggregated(sum, numSamples);
}

template <class LockTraits>
void ThreadLocalStatsMapT<LockTraits>::addHistogramValue(
    folly::StringPiece name,
    int64_t value) {
  auto state = state_.lock();
  TLHistogram* histogram = getHistogramLocked(*state, name);
  if (histogram) {
    histogram->addValue(value);
  }
}

template <class LockTraits>
void ThreadLocalStatsMapT<LockTraits>::incrementCounter(
    folly::StringPiece name,
    int64_t amount) {
  auto state = state_.lock();
  getCounterLocked(*state, name)->incrementValue(amount);
}

template <class LockTraits>
std::shared_ptr<typename ThreadLocalStatsMapT<LockTraits>::TLTimeseries>
ThreadLocalStatsMapT<LockTraits>::getTimeseriesSafe(folly::StringPiece name) {
  auto state = state_.lock();
  auto& entry = state->namedTimeseries_[name];
  if (!entry) {
    entry = std::make_shared<TLTimeseries>(this, name);
  }
  return entry;
}

template <class LockTraits>
std::shared_ptr<typename ThreadLocalStatsMapT<LockTraits>::TLTimeseries>
ThreadLocalStatsMapT<LockTraits>::getTimeseriesSafe(
    folly::StringPiece name,
    size_t numBuckets,
    size_t numLevels,
    const int levelDurations[]) {
  auto state = state_.lock();
  auto& entry = state->namedTimeseries_[name];
  if (!entry) {
    entry = std::make_shared<TLTimeseries>(
        this, name, numBuckets, numLevels, levelDurations);
  }
  return entry;
}

template <class LockTraits>
typename ThreadLocalStatsMapT<LockTraits>::TLTimeseries* ThreadLocalStatsMapT<
    LockTraits>::getTimeseriesLocked(State& state, folly::StringPiece name) {
  auto& entry = state.namedTimeseries_[name];
  if (!entry) {
    entry = std::make_shared<TLTimeseries>(this, name);
  }
  return entry.get();
}

template <class LockTraits>
typename ThreadLocalStatsMapT<LockTraits>::TLHistogram* ThreadLocalStatsMapT<
    LockTraits>::getHistogramLocked(State& state, folly::StringPiece name) {
  auto& entry = state.namedHistograms_[name];
  if (!entry) {
    // Uncommon case: We don't know about this histogram in this thread yet.
    // Look up the global histogram info.
    ExportedHistogramMapImpl::LockableHistogram globalHist =
        this->getHistogramMap()->getLockableHistogram(name);
    if (globalHist.isNull()) {
      // This histogram doesn't exist: it has never been created with
      // ServiceData::addHistogram().  Just ignore this call.
      // (This is the same behavior as ServiceData::addHistogram() when called
      // on a non-existent histogram.)
      return nullptr;
    }

    entry = std::make_shared<TLHistogram>(this, name, globalHist);
  }

  return entry.get();
}

template <class LockTraits>
std::shared_ptr<typename ThreadLocalStatsMapT<LockTraits>::TLCounter>
ThreadLocalStatsMapT<LockTraits>::getCounterSafe(folly::StringPiece name) {
  auto state = state_.lock();
  auto& entry = state->namedCounters_[name];
  if (!entry) {
    entry = std::make_shared<TLCounter>(this, name);
  }
  return entry;
}

template <class LockTraits>
typename ThreadLocalStatsMapT<LockTraits>::TLCounter* ThreadLocalStatsMapT<
    LockTraits>::getCounterLocked(State& state, folly::StringPiece name) {
  auto& entry = state.namedCounters_[name];
  if (!entry) {
    entry = std::make_shared<TLCounter>(this, name);
  }
  return entry.get();
}

} // namespace fb303
} // namespace facebook
