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
void ThreadLocalStatsMapT<LockTraits>::addStatValue(
    folly::StringPiece name,
    int64_t value,
    ExportType exportType) {
  auto state = state_.lock();
  getTimeseriesLocked(*state, name, exportType)->addValue(value);
}

template <class LockTraits>
void ThreadLocalStatsMapT<LockTraits>::clearStat(
    folly::StringPiece name,
    ExportType exportType) {
  auto state = state_.lock();
  auto& set = state->namedTimeseries_;
  if (auto it = set.find(name); it != set.end()) {
    it->type(exportType, false);
  }
  this->getServiceData()->addStatExportType(name, exportType, nullptr);
}

template <class LockTraits>
void ThreadLocalStatsMapT<LockTraits>::addHistogramValue(
    folly::StringPiece name,
    int64_t value) {
  auto state = state_.lock();
  TLHistogram* histogram = getHistogramLockedPtr(*state, name);
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
  auto& entry = tryInsertLocked(state_.lock()->namedTimeseries_, name, [&] {
    return std::shared_ptr<TLTimeseries>{new TLTimeseries(this, name)};
  });
  return entry.ptr();
}

template <class LockTraits>
std::shared_ptr<typename ThreadLocalStatsMapT<LockTraits>::TLTimeseries>
ThreadLocalStatsMapT<LockTraits>::getTimeseriesSafe(
    folly::StringPiece name,
    size_t numBuckets,
    size_t numLevels,
    const ExportedStat::Duration levelDurations[]) {
  auto& entry = tryInsertLocked(state_.lock()->namedTimeseries_, name, [&] {
    return std::shared_ptr<TLTimeseries>{
        new TLTimeseries(this, name, numBuckets, numLevels, levelDurations)};
  });
  return entry.ptr();
}

template <class LockTraits>
typename ThreadLocalStatsMapT<LockTraits>::TLTimeseries* ThreadLocalStatsMapT<
    LockTraits>::getTimeseriesLocked(State& state, folly::StringPiece name) {
  auto& entry = tryInsertLocked(state.namedTimeseries_, name, [&] {
    return std::shared_ptr<TLTimeseries>{new TLTimeseries(this, name)};
  });
  return entry.raw();
}

template <class LockTraits>
typename ThreadLocalStatsMapT<LockTraits>::TLTimeseries*
ThreadLocalStatsMapT<LockTraits>::getTimeseriesLocked(
    State& state,
    folly::StringPiece name,
    ExportType exportType) {
  auto& entry = tryInsertLocked(state.namedTimeseries_, name, [&] {
    return std::shared_ptr<TLTimeseries>{new TLTimeseries(this, name)};
  });
  if (!entry.type(exportType)) {
    this->getServiceData()->addStatExportType(name, exportType);
    entry.type(exportType, true);
  }
  return entry.raw();
}

template <class LockTraits>
void ThreadLocalStatsMapT<LockTraits>::clearTimeseriesSafe(
    folly::StringPiece name) {
  state_.lock()->namedTimeseries_.erase(name);
}

template <class LockTraits>
std::shared_ptr<typename ThreadLocalStatsMapT<LockTraits>::TLHistogram>
ThreadLocalStatsMapT<LockTraits>::getHistogramSafe(folly::StringPiece name) {
  auto state = state_.lock();
  return this->getHistogramLocked(*state, name);
}

template <class LockTraits>
typename ThreadLocalStatsMapT<LockTraits>::TLHistogram* ThreadLocalStatsMapT<
    LockTraits>::getHistogramLockedPtr(State& state, folly::StringPiece name) {
  auto& entry = tryInsertLocked(state.namedHistograms_, name, [&] {
    return this->createHistogramLocked(state, name);
  });
  return entry.raw();
}

template <class LockTraits>
std::shared_ptr<typename ThreadLocalStatsMapT<LockTraits>::TLHistogram>
ThreadLocalStatsMapT<LockTraits>::createHistogramLocked(
    State&,
    folly::StringPiece name) {
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

  return std::make_shared<TLHistogram>(this, name, globalHist);
}

template <class LockTraits>
std::shared_ptr<typename ThreadLocalStatsMapT<LockTraits>::TLHistogram>
ThreadLocalStatsMapT<LockTraits>::getHistogramLocked(
    State& state,
    folly::StringPiece name) {
  auto& entry = tryInsertLocked(state.namedHistograms_, name, [&] {
    return this->createHistogramLocked(state, name);
  });
  return entry.ptr();
}

template <class LockTraits>
std::shared_ptr<typename ThreadLocalStatsMapT<LockTraits>::TLCounter>
ThreadLocalStatsMapT<LockTraits>::getCounterSafe(folly::StringPiece name) {
  auto& entry = tryInsertLocked(state_.lock()->namedCounters_, name, [&] {
    return std::shared_ptr<TLCounter>{new TLCounter(this, name)};
  });
  return entry.ptr();
}

template <class LockTraits>
typename ThreadLocalStatsMapT<LockTraits>::TLCounter* ThreadLocalStatsMapT<
    LockTraits>::getCounterLocked(State& state, folly::StringPiece name) {
  auto& entry = tryInsertLocked(state.namedCounters_, name, [&] {
    return std::shared_ptr<TLCounter>{new TLCounter(this, name)};
  });
  return entry.raw();
}

template <class LockTraits>
void ThreadLocalStatsMapT<LockTraits>::resetAllData() {
  auto state = state_.lock();
  state->namedCounters_.clear();
  state->namedHistograms_.clear();
  state->namedTimeseries_.clear();
}

template <class LockTraits>
template <typename StatType, typename Make>
typename ThreadLocalStatsMapT<LockTraits>::template StatPtr<StatType> const&
ThreadLocalStatsMapT<LockTraits>::tryInsertLocked(
    StatMap<StatType>& map,
    folly::StringPiece name,
    Make make) {
  auto const hash = map.prehash(name);
  if (auto const iter = map.find(hash, name); iter != map.end()) {
    return *iter;
  }
  if (auto ptr = make()) {
    StatPtr<StatType> entry;
    entry.ptr(std::move(ptr));
    return *map.emplace_token(hash, std::move(entry)).first;
  }
  static auto const& empty = *new StatPtr<StatType>();
  return empty;
}

} // namespace fb303
} // namespace facebook
