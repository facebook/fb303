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

#include <fb303/ExportType.h>
#include <fb303/MutexWrapper.h>
#include <fb303/Timeseries.h>
#include <folly/container/F14Map.h>

namespace facebook {
namespace fb303 {

using CounterType = int64_t;
class DynamicCounters;
using ExportedStat = MultiLevelTimeSeries<CounterType>;

/**
 * class ExportedStatMap
 *
 * Include high-level description of class here
 */

class ExportedStatMap {
 public:
  using SyncStat = folly::Synchronized<ExportedStat, MutexWrapper>;
  using StatPtr = std::shared_ptr<SyncStat>;
  using LockedStatPtr = SyncStat::LockedPtr;
  using StatMap = folly::F14FastMap<std::string, StatPtr>;

  /*
   * Creates an ExportedStatMap and hooks it up to the given DynamicCounters
   * object for getCounters().  The defaultStat object provided will be used
   * as a blueprint for new timeseries that are created; if no 'defaultStat'
   * object is provided, MinuteTenMinuteHourTimeSeries is used.
   */
  explicit ExportedStatMap(
      DynamicCounters* counters,
      ExportType defaultType = AVG,
      const ExportedStat& defaultStat =
          MinuteTenMinuteHourTimeSeries<CounterType>())
      : dynamicCounters_(counters),
        defaultTypes_(1, defaultType),
        defaultStat_(defaultStat) {}

  ExportedStatMap(
      DynamicCounters* counters,
      const std::vector<ExportType>& defaultTypes,
      const ExportedStat& defaultStat =
          MinuteTenMinuteHourTimeSeries<CounterType>())
      : dynamicCounters_(counters),
        defaultTypes_(defaultTypes),
        defaultStat_(defaultStat) {}

  ExportedStatMap(const ExportedStatMap&) = delete;
  ExportedStatMap& operator=(const ExportedStatMap&) = delete;

  /*
   * Set defaultStat_ field.
   */
  void setDefaultStat(const ExportedStat& defaultStat) {
    defaultStat_ = defaultStat;
  }

  /*
   * Set defaultTypes_ field.
   */
  void setDefaultTypes(const std::vector<ExportType>& types) {
    defaultTypes_ = types;
  }

  /*
   * Get dynamicCounters_ field.
   */
  DynamicCounters* dynamicCounters() const {
    return dynamicCounters_;
  }

  /*
   * Returns a pointer to a LockedStatPtr that holds the locked ExportedStat
   * specified by 'name.' The ExportedStat object can be accessed using the '->'
   * operator and any modifications to it will modify the value stored
   * in the map.
   *
   * When the returned pointer is destroyed, the object's mutex is released. In
   * other words, a thread will maintain exclusive control over the given key
   * until it destroyes the returned pointer.
   *
   * The data may be stale so flush() is called on the ExportedStat object
   * before reading data off of it.
   *
   * --
   * NOTE: Do _not_ store the returned pointer for a long time.  This will
   * keep the mutex locked and not allow anyone else to access this item.
   * --
   */
  LockedStatPtr getLockedStatPtr(folly::StringPiece name) {
    auto result = getStatPtr(name)->lock();
    result->flush();
    return result;
  }

  /*
   * Returns the StatPtr object specified by 'name' if it exists in the map
   * and creates one with defaultStat_ if the value is missing from the map. If
   * it is newly created, this method exports the stat using the given
   * ExportType. If no ExportType is provided, defaultTypes_ is used.
   *
   * The returned StatPtr is unlocked.
   */
  StatPtr getStatPtr(
      folly::StringPiece name,
      const ExportType* exportType = nullptr);

  /*
   * Returns the StatPtr object specified by 'name' if it exists in the map
   * and creates one with copyMe if the value is missing from the map. If
   * copyMe is null, defaultStat_ is used. Unlike getStatItem() it does not
   * automatically export the stat.
   *
   * The returned StatPtr is unlocked.
   */
  StatPtr getStatPtrNoExport(
      folly::StringPiece name,
      bool* createdPtr = nullptr,
      const ExportedStat* copyMe = nullptr);

  /*
   * Export the stat specified by 'name' using each of the ExportTypes in
   * defaultTypes_. If the stat is not found in the map, a new one is created
   * using defaultStat_.
   */
  void exportStat(folly::StringPiece name) {
    for (auto type : defaultTypes_) {
      exportStat(name, type, &defaultStat_);
    }
  }

  /*
   * Adds value into the stat specified by 'name.' If none is found in the map,
   * a new one is created and exported using ExportType type first.
   */
  void addValue(
      folly::StringPiece name,
      time_t now,
      CounterType value,
      ExportType type) {
    getStatPtr(name, &type)->lock()->addValue(now, value);
  }

  /*
   * Adds multiple copies of value into the stat specified by 'name.' If none
   * is found in the map, a new one is created and exported first.
   */
  void addValue(
      folly::StringPiece name,
      time_t now,
      CounterType value,
      int64_t times = 1) {
    getStatPtr(name)->lock()->addValue(now, value, times);
  }

  /*
   * Adds aggregated value into the stat specified by 'name.' If none
   * is found in the map, a new one is created and exported first.
   */
  void addValueAggregated(
      folly::StringPiece name,
      time_t now,
      CounterType sum,
      int64_t nsamples) {
    getStatPtr(name)->lock()->addValueAggregated(now, sum, nsamples);
  }

  /*
   * Removes all entries from the map specified by 'name.'
   */
  void clearValue(folly::StringPiece name) {
    getStatPtr(name)->lock()->clear();
  }

  /*
   * Checks if the map contains key.  Note that this state might change
   * at any time (immediately) after returning.
   */
  bool contains(folly::StringPiece name) const {
    auto lockedStatMap = statMap_.rlock();
    return lockedStatMap->find(name) != lockedStatMap->end();
  }

  /*
   * Exports the stat specified by 'name' using the type provided. If no such
   * stat exists in the map, a new one is created using copyMe. defaultStat_ is
   * used when copyMe is not given.
   */
  void exportStat(
      folly::StringPiece name,
      ExportType type,
      const ExportedStat* copyMe = nullptr);

  /*
   * Unexports stats of all types with the specified name and removes it from
   * the map.
   */
  void unExportStatAll(folly::StringPiece name);

  /*
   * Clear the stats map.
   *
   * Note: This method should be used with care.  It only clears the internal
   * statMap_, but does not remove the exported stats from the DynamicCounters
   * map.  The DynamicCounters map will still report the old exported values
   * until it is cleared by the caller.
   *
   * (This is implemented this way since the DynamicCounters map may contain
   * other data besides the exported stats, and it isn't easy to find and
   * remove only the dynamic counters which are exported by this stat map.
   * This method should only be used when the caller plans on clearing the
   * entire DynamicCounters map anyway.)
   */
  void forgetAllStats();

  /*
   * Removes the stat specified by 'name' from the map.
   */
  void forgetStatsFor(folly::StringPiece name);

  /*
   * Reset all of the exported timeseries so they contain no data points.
   */
  void clearAllStats();

 protected:
  folly::Synchronized<StatMap> statMap_;
  DynamicCounters* dynamicCounters_;

  std::vector<ExportType> defaultTypes_;
  // note: We slice defaultStat by copying it to a ExportedStat
  // (non-pointer, non-reference), but that's according to plan: the
  // derived classes only set data members in the base class, nothing
  // more (they have no data members of their own).
  ExportedStat defaultStat_;
};
} // namespace fb303
} // namespace facebook
