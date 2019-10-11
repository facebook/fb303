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

#include <chrono>

#include <fb303/ThreadLocalStats.h>
#include <folly/Range.h>
#include <folly/ThreadLocal.h>
#include <folly/container/F14Map.h>
#include <folly/hash/Hash.h>

namespace facebook {
namespace fb303 {

/**
 * ThreadLocalStatsMap subclasses ThreadLocalStats, and provides APIs for
 * updating statistics by name.
 *
 * This makes it fairly easy to use as a drop-in replacement for call sites
 * that were previously using the ServiceData APIs to update stats by name.
 * A ThreadLocalStatsMap object can be used instead of the fbData singleton,
 * and the stat update operations will become faster thread local operations in
 * order to reduce or eliminate lock contention.
 *
 * However, note that accessing statistics by name is less efficient than
 * defining your own TLTimeseries or TLHistogram counters and accessing them
 * directly, as it requires a string lookup on each update operation.  Where
 * possible, prefer to define your own ThreadLocalStats subclass so you can
 * directly access counter member variables rather than having to perform
 * string lookups on each update operation.
 */
template <class LockTraits>
class ThreadLocalStatsMapT : public ThreadLocalStatsT<LockTraits> {
 public:
  using TLCounter = TLCounterT<LockTraits>;
  using TLHistogram = TLHistogramT<LockTraits>;
  using TLTimeseries = TLTimeseriesT<LockTraits>;

  explicit ThreadLocalStatsMapT(ServiceData* serviceData = nullptr);

  /**
   * Add a value to the timeseries statistic with the specified name.
   *
   * Note that you must call ServiceData::addStatExportType() in order to
   * specify which statistics about this timeseries should be reported in the
   * fb303 counters.
   */
  void addStatValue(folly::StringPiece name, int64_t value = 1);
  void addStatValueAggregated(
      folly::StringPiece name,
      int64_t sum,
      int64_t numSamples);

  /**
   * Add a value to the histogram with the specified name.
   *
   * Note that you must have first called ServiceData::addHistogram() in order
   * to specify the range and bucket width for this histogram.  If this
   * histogram has not been defined with addHistogram() yet, this call will
   * simply be ignored.
   */
  void addHistogramValue(folly::StringPiece name, int64_t value);

  /**
   * Increment a "regular-style" flat counter (no historical stats)
   */
  void incrementCounter(folly::StringPiece name, int64_t amount = 1);

  /*
   * Gets the TLTimeseries with the given name. Never returns NULL. The
   * TLTimeseries returned should not be shared with other threads.
   */
  std::shared_ptr<TLTimeseries> getTimeseriesSafe(folly::StringPiece name);
  std::shared_ptr<TLTimeseries> getTimeseriesSafe(
      folly::StringPiece name,
      size_t numBuckets,
      size_t numLevels,
      const int levelDurations[]);

  /**
   * Gets the TLCounter with the given name. Never returns NULL. The
   * TLCounter returned should not be shared with other threads.
   */
  std::shared_ptr<TLCounter> getCounterSafe(folly::StringPiece name);

 private:
  /*
   * This "lock" protects the named maps.  Since the maps should only ever be
   * accessed from a single thread, it doesn't provide real locking, but
   * instead only asserts that the accesses occur from the correct thread.
   *
   * If we used TLStatsThreadSafe::RegistryLock this would make the code truly
   * thread safe, so that any thread could update stats by name.  We could turn
   * this into a template parameter in the future, but for now no one needs the
   * fully thread-safe behavior.
   */
  using NamedMapLock = typename TLStatsNoLocking::RegistryLock;

  template <class StatType>
  using StatMap = folly::F14FastMap<std::string, std::shared_ptr<StatType>>;

  struct State;

  /*
   * Get the TLTimeseries with the given name.
   *
   * Must be called with the state lock held.
   *
   * Never returns NULL.
   */
  TLTimeseries* getTimeseriesLocked(State& state, folly::StringPiece name);

  /*
   * Get the TLHistogram with the given name.
   *
   * Must be called with the state lock held.
   *
   * May return NULL if no histogram with this name has been created in the
   * global ExportedHistogramMapImpl.  (If no histogram exists, this function
   * cannot automatically create one without knowing the histogram min, max,
   * and bucket width.)
   */
  TLHistogram* getHistogramLocked(State& state, folly::StringPiece name);

  /*
   * Get the TLCounter with the given name.
   *
   * Must be called with the state lock held.
   *
   * Never returns NULL.
   */
  TLCounter* getCounterLocked(State& state, folly::StringPiece name);

  struct State {
    StatMap<TLTimeseries> namedTimeseries_;
    StatMap<TLHistogram> namedHistograms_;
    StatMap<TLCounter> namedCounters_;
  };

  folly::Synchronized<State, NamedMapLock> state_;
};

} // namespace fb303
} // namespace facebook

#include <fb303/ThreadLocalStatsMap-inl.h>
