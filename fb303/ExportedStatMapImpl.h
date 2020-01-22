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

#include <fb303/ExportedStatMap.h>

namespace facebook {
namespace fb303 {

class ExportedStatMapImpl : public ExportedStatMap {
 public:
  /*
   * LockableStat is a simple wrapper class for the StatPtr object that
   * abstracts out all of the locking, which previously had to be done by the
   * user. This new wrapper class also allows for addValue methods to be
   * directly called on it which will clean up the current counter call sites.
   */
  class LockableStat {
   public:
    /*
     * Creates a LockableStat object from a StatPtr object. StatPtr is a
     * std::shared_ptr holding the SyncStat so the default constructor
     * initializes the stat_ field as a nullptr.
     */
    LockableStat() {}
    explicit LockableStat(StatPtr stat) : stat_(std::move(stat)) {}

    /*
     * Locks the timeseries object that is held by this LockableStat object
     * and returns the LockedPtr for it. The histogram remains locked as long as
     * the LockedPtr object remains in scope.
     */
    LockedStatPtr lock() const {
      return stat_->lock();
    }

    /* Return true if the stat held by this object is null. */
    bool isNull() const {
      return stat_ == nullptr;
    }

    /* Return the StatPtr object held by the LockableStat. */
    StatPtr getStatPtr() const {
      return stat_;
    }

    /* Swap the StatPtrs held by the LockableStat objects. */
    void swap(LockableStat& s) noexcept {
      stat_.swap(s.stat_);
    }

    /*
     * Add a value at time 'now' to all levels. The optional 'times' parameter
     * can be used here to add multiple copies of the value at a time. The
     * StatPtr remains locked for the duration of this call.
     */
    void addValue(time_t now, const CounterType value, int64_t times = 1)
        const {
      stat_->lock()->addValue(std::chrono::seconds(now), value, times);
    }
    void addValue(
        std::chrono::seconds now,
        const CounterType value,
        int64_t times = 1) const {
      stat_->lock()->addValue(now, value, times);
    }

    /*
     * Add a value at time 'now' to all levels as the sum of 'nsamples'
     * samples. The StatPtr remains locked for the duration of this call.
     */
    void addValueAggregated(
        time_t now,
        const CounterType value,
        int64_t numSamples) const {
      stat_->lock()->addValueAggregated(
          std::chrono::seconds(now), value, numSamples);
    }

    void addValueAggregated(
        std::chrono::seconds now,
        const CounterType value,
        int64_t numSamples) const {
      stat_->lock()->addValueAggregated(now, value, numSamples);
    }

    /*
     * Add a value at time 'now' to all levels. The optional 'times' parameter
     * can be used here to add multiple copies of the value at a time.
     *
     * This method assumes that the object has already been locked, and requires
     * the appropriate LockedPtr object as a parameter.
     */
    void addValueLocked(
        const LockedStatPtr& lockedObj,
        time_t now,
        const CounterType value,
        int64_t times = 1) const {
      DCHECK(!lockedObj.isNull());
      lockedObj->addValue(now, value, times);
    }

    /**
     * Update the histogram with the given time value.
     *
     * This method assumes that the object has already been locked, and requires
     * the appropriate LockedPtr object as a parameter.
     */
    void updateLocked(const LockedStatPtr& lockedObj, time_t now) {
      DCHECK(!lockedObj.isNull());
      lockedObj->update(now);
    }

    /*
     * Flush all cached updates.
     *
     * This method assumes that the object has already been locked, and requires
     * the appropriate LockedPtr object as a parameter.
     */
    void flushLocked(const LockedStatPtr& lockedObj) {
      DCHECK(!lockedObj.isNull());
      lockedObj->flush();
    }

    /*
     * Return the rate (sum divided by elaspsed time) of the all data points
     * currently tracked at this level.
     */
    template <typename ReturnType>
    ReturnType rate(int level) {
      return stat_->lock()->rate<ReturnType>(level);
    }

    /*
     * Return the sum of all the data points currently tracked at this level.
     *
     * This method assumes that the object has already been locked, and requires
     * the appropriate LockedPtr object as a parameter.
     */
    CounterType getSumLocked(const LockedStatPtr& lockedObj, int level) {
      DCHECK(!lockedObj.isNull());
      return lockedObj->sum(level);
    }

   private:
    StatPtr stat_;
  };

  /*
   * Creates an ExportedStatMapImpl and hooks it up to the given DynamicCounters
   * object for getCounters().  The defaultStat object provided will be used
   * as a blueprint for new timeseries that are created; if no 'defaultStat'
   * object is provided, MinuteTenMinuteHourTimeSeries is used.
   */
  explicit ExportedStatMapImpl(
      DynamicCounters* counters,
      ExportType defaultType = AVG,
      const ExportedStat& defaultStat =
          MinuteTenMinuteHourTimeSeries<CounterType>())
      : ExportedStatMap(counters, defaultType, defaultStat) {}

  ExportedStatMapImpl(
      DynamicCounters* counters,
      const std::vector<ExportType>& defaultTypes,
      const ExportedStat& defaultStat =
          MinuteTenMinuteHourTimeSeries<CounterType>())
      : ExportedStatMap(counters, defaultTypes, defaultStat) {}

  /*
   * Returns the StatPtr object specified by 'name' if it exists in the map
   * and creates one with defaultStat_ if the value is missing from the map. If
   * it is newly created, this method exports the stat using the given
   * ExportType. If no ExportType is provided, defaultTypes_ is used.
   *
   * The returned StatPtr is unlocked.
   */
  LockableStat getLockableStat(
      folly::StringPiece name,
      const ExportType* type = nullptr);

  /*
   * Returns the StatPtr object specified by 'name' if it exists in the map
   * and creates one with copyMe if the value is missing from the map. If
   * copyMe is null, defaultStat_ is used. Unlike getStatItem() it does not
   * automatically export the stat.
   *
   * The returned StatPtr is unlocked.
   */
  LockableStat getLockableStatNoExport(
      folly::StringPiece name,
      bool* createdPtr = nullptr,
      const ExportedStat* copyMe = nullptr);

  using ExportedStatMap::addValue;
  /*
   * Adds multiple copies of value into the stat specified by 'name.' If none
   * is found in the map, a new one is created and exported first.
   *
   * This method is identical to the addValue method in ExportedStatMap but is
   * more efficient because it avoids hashtable lookup per operation.
   *
   * This method is primarily left here for backwards compatibility. Users
   * should prefer calling the addValue method directly on the StatPtr object
   * using the -> operator instead of using this method.
   */
  void
  addValue(StatPtr& item, time_t now, CounterType value, int64_t times = 1) {
    item->lock()->addValue(now, value, times);
  }

  using ExportedStatMap::addValueAggregated;
  /*
   * Adds aggregated value into the stat specified by 'name.' If none
   * is found in the map, a new one is created and exported first.
   *
   * This method is identical to the addValueAggregated method in
   * ExportedStatMap but is more efficient because it avoids hashtable lookup
   * per operation.
   *
   * This method is primarily left here for backwards compatibility. Users
   * should prefer calling the addValueAggregated method directly on the StatPtr
   * object using the -> operator instead of using this method.
   */
  void addValueAggregated(
      StatPtr& item,
      time_t now,
      CounterType sum,
      int64_t nsamples) {
    item->lock()->addValueAggregated(now, sum, nsamples);
  }

  using ExportedStatMap::exportStat;
  /*
   * Exports the stat specified by 'name' using the type provided. If no such
   * stat exists in the map, a new one is created using copyMe. defaultStat_ is
   * used when copyMe is not given.
   *
   * This method is identical to the exportStat method in ExportedStatMap but is
   * more efficient because it avoids hashtable lookup per operation.
   *
   * This method is primarily left here for backwards compatibility. Users
   * should prefer using the StatPtr object instead of LockableStat.
   */
  void exportStat(LockableStat stat, folly::StringPiece name, ExportType type);
};

} // namespace fb303
} // namespace facebook
