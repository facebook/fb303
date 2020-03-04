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

#include <fb303/DynamicCounters.h>
#include <fb303/ExportType.h>
#include <fb303/MutexWrapper.h>
#include <fb303/TimeseriesHistogram.h>
#include <folly/MapUtil.h>
#include <folly/experimental/StringKeyedUnorderedMap.h>
#include <folly/small_vector.h>

namespace facebook {
namespace fb303 {

using ExportedHistogram = TimeseriesHistogram<CounterType>;
using ExportedStat = MultiLevelTimeSeries<CounterType>;

/**
 * class ExportedHistogramMap
 *
 * This class implements a map(string => TimeseriesHistogram), with the
 * ability to export data from these histograms to our FB303 counters
 * and exported values facilities so that this data can be collected in
 * an automated fashion by dashboards or ODS.
 *
 * You can create new histograms via 'exportHistogram()', which also exports
 * the full histogram to the fb303::getExportedValues() call via the
 * DynamicStrings connector object.
 *
 * You can also export a simple counter via the fb303::getCounters() call
 * for a particular percentile in your histogram via exportPercentile().
 *
 * After creation, you can use the addValue*() functions to insert values.
 *
 * The values can get queried by using 'getHistogram()' and then querying
 * functions on the histogram directly, or via the various counters exported.
 *
 * Supports percentiles from p[0, 100].
 * p100 is value for the largest non-empty bucket. See D1984528
 *
 * @author mrabkin
 */

class ExportedHistogramMap {
 public:
  using SyncHistogram = folly::Synchronized<ExportedHistogram, MutexWrapper>;
  using HistogramPtr = std::shared_ptr<SyncHistogram>;
  using LockedHistogramPtr = SyncHistogram::LockedPtr;
  using HistMap = folly::StringKeyedUnorderedMap<HistogramPtr>;

  /**
   * Creates an ExportedHistogramMap and hooks it up to the given
   * DynamicCounters object for getCounters(), and the given DynamicStrings
   * object for getExportedValues().  The copyMe object provided will be used
   * as a blueprint for new histograms that are created; this is where you set
   * up your bucket ranges and time levels appropriately for your needs.  There
   * is no default set of bucket ranges, so a 'copyMe' object must be provided.
   */
  ExportedHistogramMap(
      DynamicCounters* counters,
      DynamicStrings* strings,
      const ExportedHistogram& copyMe);
  ExportedHistogramMap(const ExportedHistogramMap&) = delete;
  ExportedHistogramMap& operator=(const ExportedHistogramMap&) = delete;

  /**
   * Set defaultHist_ field.
   */
  void setDefaultHistogram(const ExportedHistogram& copyMe) {
    defaultHist_ = copyMe;
  }

  /**
   * Set defaultStat_ field.
   */
  void setDefaultStat(const ExportedStat& defaultStat) {
    defaultStat_ = defaultStat;
  }

  const ExportedStat& getDefaultStat() const {
    return defaultStat_;
  }

  /**
   * Returns true if the given histogram exists in the map
   */
  bool contains(folly::StringPiece name) const {
    auto lockedHistMap = histMap_.lock();
    return lockedHistMap->find(name) != lockedHistMap->end();
  }

  /**
   * Returns a LockedHistogramPtr to the given histogram that holds a lock
   * while it exists.  Please destroy this pointer as soon as possible to
   * release the lock and allow updates to the histogram from other threads.
   *
   * If the histogram doesn't exist, returns an empty LockedHistogramPtr.
   */
  LockedHistogramPtr getHistogram(folly::StringPiece name) {
    auto hist = getHistogramUnlocked(name);
    if (hist) {
      return hist->lock();
    }
    return LockedHistogramPtr();
  }

  /**
   * Get a HistogramPtr object from histMap_ which is unlocked.
   *
   * If the histogram doesn't exist, returns a nullptr.
   */
  HistogramPtr getHistogramUnlocked(folly::StringPiece name) {
    auto lockedHistMap = histMap_.lock();
    return folly::get_default(*lockedHistMap, name);
  }

  /**
   * Get a HistogramPtr object from histMap_. If this histogram does not exist,
   * create it by copying the specified copyMe argument, and automatically
   * export it.
   *
   * The caller must lock the HistogramPtr before modifying the histogram.
   */
  HistogramPtr getOrCreateUnlocked(
      folly::StringPiece name,
      const ExportedHistogram* copyMe,
      bool* createdPtr = nullptr);

  /**
   * Creates a new histogram with the given name.
   *
   * Returns true if a new histogram was created, and false if a histogram with
   * this name already existed.  Logs an error if a histogram already existed
   * with this name but had different parameters.
   *
   * If 'copyMe' is provided, the new histogram is copy-constructed from
   * 'copyMe' and then cleared.  Otherwise, the histogram is constructed in the
   * same way from the 'copyMe' argument provided to the ExportedHistogramMap
   * constructor.
   *
   * Then, all of the histogram's levels are all exported to DynamicStrings
   * with keys of the form:   <histogram_name>.hist.<level_duration>
   */
  bool addHistogram(
      folly::StringPiece name,
      const ExportedHistogram* copyMe = nullptr);

  bool addHistogram(
      folly::StringPiece name,
      int64_t bucketWidth,
      int64_t min,
      int64_t max);

  /**
   * Given a histogram, exports a counter representing our best estimate of the
   * given percentile's value in the histogram at all time levels. If the given
   * histogram doesn't exist, returns false.
   */
  bool exportPercentile(folly::StringPiece name, int percentile);

  /**
   * Given a histogram, export multiple percentile values.
   */
  template <typename... Percentiles>
  bool exportPercentile(
      folly::StringPiece name,
      int percentile,
      Percentiles... morePercentiles) {
    return (
        exportPercentile(name, percentile) &&
        exportPercentile(name, morePercentiles...));
  }

  /**
   * Unexport a histogram percentile previously exported with
   * exportPercentile().
   *
   * This does not clear any data, merely stops reporting the percentile in the
   * DynamicCounters map.
   */
  void unexportPercentile(folly::StringPiece name, int percentile);

  /**
   * Given a histogram, unexport multiple percentile values.
   */
  template <typename... Percentiles>
  void unexportPercentile(
      folly::StringPiece name,
      int percentile,
      Percentiles... morePercentiles) {
    unexportPercentile(name, percentile);
    unexportPercentile(name, morePercentiles...);
  }

  /**
   * Given a histogram, export the specified statistic in the histogram at all
   * time levels.  If the given histogram doesn't exist, returns false.
   */
  bool exportStat(folly::StringPiece name, ExportType type);

  /**
   * Also accept exportStat() with an integer argument as an alias for
   * exportPercentile().
   *
   * This allows the exportStat() function below to be called with a mixture
   * of integer and percentile arguments.
   */
  bool exportStat(folly::StringPiece name, int percentile) {
    return exportPercentile(name, percentile);
  }

  /**
   * Given a histogram, export multiple statistics.
   *
   * For convenience, this accepts a mixture of ExportType and integer
   * percentile arguments, to allow exporting both ExportType stats and
   * percentiles in a single call.
   */
  template <typename TypeOrPercentile, typename... ExportArgs>
  bool exportStat(
      folly::StringPiece name,
      TypeOrPercentile stat,
      ExportArgs... moreArgs) {
    return exportStat(name, stat) && exportStat(name, moreArgs...);
  }

  /*
   * Unexport a histogram statistic previously exported with exportStat().
   *
   * This does not clear any data, merely stops reporting the statistic in the
   * DynamicCounters map.
   */
  void unexportStat(folly::StringPiece name, ExportType type);

  /**
   * unexportStat() for percentiles.
   */
  void unexportStat(folly::StringPiece name, int percentile) {
    unexportPercentile(name, percentile);
  }

  /**
   * Given a histogram, unexport multiple statistics.
   *
   * This accepts a mixture of ExportType and integer percentile arguments.
   */
  template <typename TypeOrPercentile, typename... ExportArgs>
  void unexportStat(
      folly::StringPiece name,
      TypeOrPercentile stat,
      ExportArgs... moreArgs) {
    unexportStat(name, stat);
    unexportStat(name, moreArgs...);
  }

  /**
   * Adds a value into histogram 'name' at time 'now' without looking up the
   * histogram from the map
   */
  void addValue(
      folly::StringPiece name,
      std::chrono::seconds now,
      CounterType value,
      int64_t times = 1) {
    HistogramPtr hist = getHistogramUnlocked(name);
    if (hist) {
      hist->lock()->addValue(now, value, times);
    }
  }

  void addValues(
      folly::StringPiece name,
      std::chrono::seconds now,
      const folly::Histogram<CounterType>& values) {
    HistogramPtr hist = getHistogramUnlocked(name);
    if (hist) {
      hist->lock()->addValues(now, values);
    }
  }

  /**
   * If a named histogram is not existed, add one and export given percentile
   */
  void addValues(
      folly::StringPiece name,
      std::chrono::seconds now,
      const folly::Histogram<CounterType>& values,
      const ExportedHistogram* hist,
      int percentile) {
    addValues(name, now, values, hist, folly::small_vector<int>(1, percentile));
  }

  /**
   * If a named histogram is not existed, add one and export given percentiles
   */
  void addValues(
      folly::StringPiece name,
      std::chrono::seconds now,
      const folly::Histogram<CounterType>& values,
      const ExportedHistogram* hist,
      folly::small_vector<int> percentiles) {
    bool created;
    getOrCreateUnlocked(name, hist, &created);
    if (created) {
      for (auto percentile : percentiles) {
        exportPercentile(name, percentile);
      }
    }
    addValues(name, now, values);
  }

  /**
   * Clears the histogram with the given name.
   */
  void clearHistogram(folly::StringPiece name) {
    HistogramPtr hist = getHistogramUnlocked(name);
    if (hist) {
      hist->lock()->clear();
    }
  }

  /**
   * Clear all of the histograms
   */
  void clearAllHistograms();

  /*
   * Remove all of the histograms from the map.
   *
   * Note: This method should be used with care.  It only removes the
   * histograms, but does not actually unexport them from the DynamicCounters
   * and DynamicStrings maps.  The DynamicCounters and DynamicStrings will
   * still report the old exported histogram values until they are also cleared
   * by the caller.
   *
   * (This is implemented this way since the DynamicCounters and DynamicStrings
   * maps may contain other data besides the exported histograms, and it isn't
   * easy to find and remove only the dynamic values which are exported by
   * this histogram map.  This method should only be used when the caller plans
   * on clearing the entire DynamicCounters and DynamicStrings maps anyway.)
   */
  void forgetAllHistograms() {
    histMap_.lock()->clear();
  }

  /*
   * Same as above, but for a single histogram identified by the name. The
   * DynamicCounters and DynamicStrings will still report the old exported
   * histogram values until they are also cleared by the caller.
   *
   */
  void forgetHistogramsFor(folly::StringPiece name) {
    histMap_.lock()->erase(name);
  }

 protected:
  void checkAdd(
      folly::StringPiece name,
      const HistogramPtr& item,
      int64_t bucketWidth,
      int64_t min,
      int64_t max) const;

  folly::Synchronized<HistMap, MutexWrapper> histMap_;

  DynamicCounters* dynamicCounters_;
  DynamicStrings* dynamicStrings_;
  ExportedHistogram defaultHist_;
  // note: We slice defaultStat by copying it to a ExportedStat
  // (non-pointer, non-reference), but that's according to plan: the
  // derived classes only set data members in the base class, nothing
  // more (they have no data members of their own).
  ExportedStat defaultStat_;
};

// Lock the histogram and calculate the percentile
CounterType getHistogramPercentile(
    const ExportedHistogramMap::HistogramPtr& hist,
    int level,
    double percentile);
} // namespace fb303

} // namespace facebook
