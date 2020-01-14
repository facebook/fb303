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

#include <fb303/ExportedHistogramMapImpl.h>
#include <fb303/ExportedStatMapImpl.h>

#include <fb303/DynamicCounters.h>
#include <fb303/detail/QuantileStatMap.h>
#include <folly/Optional.h>
#include <folly/Range.h>
#include <folly/Synchronized.h>
#include <folly/container/F14Map.h>

#include <atomic>
#include <chrono>
#include <cinttypes>
#include <map>
#include <string>
#include <vector>

namespace facebook {
namespace fb303 {

/**
 * ServiceData stores statistics and other information used by most
 * Facebook C++ services.
 *
 * This data includes timeseries counters, histograms, flat counters,
 * exported string values, options, etc.
 *
 * ServiceData is a singleton: a single instance is created when the program is
 * started, and remains in existence until the program exits.  The global
 * ServiceData instance can be accessed via the static ServiceData::get()
 * method, or via the global facebook::fbData pointer.
 */
class ServiceData {
 public:
  /**
   * Create a new ServiceData instance.
   *
   * Note: You normally should never need to create a new ServiceData object.
   * A global instance will be created automatically and is accessible via
   * facebook::fbData and ServiceData::get().
   *
   * This constructor should only be needed if you really want to keep a
   * separate set of statistics that is completely independent from the normal
   * global ServiceData instance.
   */
  ServiceData();

  /**
   * Destructor.
   *
   * Note: you should only manually destroy extra ServiceData objects, never
   * the global fbData singleton.
   */
  virtual ~ServiceData();

  /**
   * A pointer to the global ServiceData singleton, free of the
   * 'static initialization order fiasco' (SIOF).
   *
   * NOTE: use this whenever you need to reference the singleton
   * from any static initializer (e.g.: from another singleton's
   * initialization). If you're not referencing this method from
   * a static initializer (i.e.: anything that's guaranteed to
   * only run after entering main()), then it is preferable to
   * use the global facebook::fbData instead.
   *
   * @author: Marcelo Juchem <marcelo@fb.com>
   */
  static ServiceData* get();

  static std::shared_ptr<ServiceData> getShared();

  ServiceData(const ServiceData&) = delete;
  ServiceData& operator=(const ServiceData&) = delete;

  std::chrono::seconds getAliveSince() const {
    return aliveSince_;
  }

  ExportedStatMapImpl* getStatMap() {
    return &statsMap_;
  }
  ExportedHistogramMapImpl* getHistogramMap() {
    return &histMap_;
  }
  DynamicCounters* getDynamicCounters() {
    return &dynamicCounters_;
  }
  DynamicStrings* getDynamicStrings() {
    return &dynamicStrings_;
  }
  fb303::detail::QuantileStatMap* getQuantileStatMap() {
    return &quantileMap_;
  }

  /**
   * Clear all counters, histograms, exported values, options, etc.
   *
   * Counters and histograms are completely forgotten about, not simply reset
   * to 0.  You must call addStatExportType()/addStatExports()/addHistogram()
   * again to re-add the statistics.
   */
  void resetAllData();

  /**
   * Zero-out all timeseries counters, histograms, and flat counters.
   */
  void zeroStats();

  /**
   * Exports the given stat value to the counters, using the given export
   * type. In other words, after calling for key = "foo", calls to
   * getCounters() will contain several counters of the form:
   *
   * type AVG:   foo.avg    foo.avg.60    foo.avg.600    foo.avg.3600
   * type SUM:   foo.sum    foo.sum.60    foo.sum.600    foo.sum.3600
   * type RATE:  foo.rate   foo.rate.60   foo.rate.600   foo.rate.3600
   * type COUNT: foo.count  foo.count.60  foo.count.600  foo.count.3600
   *
   * The values for these counters will be computed, of course, from data
   * inserted via calls to addStatValue("foo", val).
   *
   * Here's a guide to what each export type produces:
   *
   *   addStatExportType("foo", AVG);    // == SUM / COUNT
   *   addStatExportType("foo", SUM);    // tracks sum of values inserted
   *   addStatExportType("foo", RATE);   // == SUM / (time period in secs)
   *   addStatExportType("foo", COUNT);  // tracks # of values inserted
   *
   * Note that this function can be called multiple times for a given key
   * to create multiple export counter types.  Also note that if this function
   * is not called at all for a given key prior to data insertion, the key will
   * be auto- exported as AVG by default.
   */
  void addStatExportType(
      folly::StringPiece key,
      ExportType exportType = AVG,
      const ExportedStat* statPrototype = nullptr);

  /**
   *  Convenience function for simultaneously adding and exporting stats and a
   *  histogram with several percentiles at once.  The 'stats' input string is a
   *  comma separated list of stats and integers, e.g. "AVG,75,95" would
   *  export the average as well as the 75th and 95th percentiles.  If no
   *  percentiles are given, a histogram is not created.  Throws exceptions with
   *  invalid input or malformed 'stats' strings.
   *
   * NOTE: This function is deprecated and should not be used.  Just use
   * exportHistogram() instead.  e.g.,
   *
   *   addHistogram("myhistogram", bucketWidth, min, max);
   *   exportHistogram("myhistogram", 50, 95, 99, 100, SUM, AVG, COUNT);
   *
   * Using exportHistogram() will avoid having to track duplicate data in both
   * a histogram and a timeseries dataset.
   */
  void addStatExports(
      folly::StringPiece key,
      folly::StringPiece stats,
      int64_t bucketWidth = 0,
      int64_t min = 0,
      int64_t max = 0,
      const ExportedStat* statPrototype = nullptr);

  /**
   * Adds a value to the historical statistics for a given key.
   *
   * Please see the documentation for addStatExportType() to see how to make
   * these statistics accessible via calls to getCounters().
   *
   * Also, you can query these statistics via calls similar to:
   *
   *  serviceData->getStatMap()->getStat("keyName")->get{Sum,Avg,Rate,Count}
   *
   * See the documentation of common/stats/Timeseries.h for more info.
   * Note that the optional exportType parameter is only used if the
   * stat has not already been added.
   */
  void addStatValue(folly::StringPiece key, int64_t value = 1);
  void
  addStatValue(folly::StringPiece key, int64_t value, ExportType exportType);
  void addStatValueAggregated(
      folly::StringPiece key,
      int64_t sum,
      int64_t numSamples);

  /**
   * Defines a histogram that can be used via calls to addHistogramValue() and
   * further configured via exportHistogramPercentile().  Registers the
   * histogram so that its buckets are retrievable via getExportedValues().
   *
   * The histogram will contain buckets 'bucketWidth' wide, with the lowest
   * bucket starting at 'min' and highest ending at 'max'.  Two additional
   * buckets will be implicitly created for data "below min" and "above max".
   *
   * The default histogram will have the standard 60/600/3600/inf bucketing;
   * if you desire different time buckets for your histogram, use the other
   * form of 'addHistogram()' which allows you to pass a prototype.
   *
   * Returns true if a new histogram was created, or false if a histogram with
   * this name already existed.
   */
  bool addHistogram(
      folly::StringPiece key,
      int64_t bucketWidth,
      int64_t min,
      int64_t max);
  /**
   * Defines a histogram that can be used via calls to addHistogramValue() and
   * further configured via exportHistogramPercentile().  Registers the
   * histogram so that its buckets are retrievable via getExportedValues().
   *
   * The new histogram's bucket arrangement (and the number and duration of
   * timeseries levels within the buckets) will be copied from the prototype
   * histogram.
   *
   * Please see the documentation for ExportedHistogram to see how to
   * construct different bucket arrangements and time series levels.
   *
   * Returns true if a new histogram was created, or false if a histogram with
   * this name already existed.
   */
  bool addHistogram(folly::StringPiece key, const ExportedHistogram& prototype);

  /**
   * Given a histogram already created with addHistogram(), exports a stat value
   * counter of the form: "<key>.hist.p<pct>.<duration>" which is very similar
   * to the counters defined by addStatExportType(), except that instead of
   * other aggregation methods like 'sum' or 'avg, the aggregation method is
   * finding the correct percentile in the dataset, such as 'p95' for example.
   *
   * For instance, a call like: exportHistogramPercentile("req_latency", 95)
   * will result in the following counters being exported:
   *
   *   req_latency.hist.p95
   *   req_latency.hist.p95.60
   *   req_latency.hist.p95.600
   *   req_latency.hist.p95.3600
   */
  void exportHistogramPercentile(folly::StringPiece key, int pct);

  /**
   * A version of exportHistogramPercentile() that accepts multiple percentile
   * arguments, to export many percentiles with just a single call.
   */
  template <typename... Args>
  void exportHistogramPercentile(
      folly::StringPiece key,
      int pct,
      const Args&... args) {
    exportHistogramPercentile(key, pct);
    exportHistogramPercentile(key, args...);
  }

  /*
   * Convenience template for exporting both stats and percentile values
   * from a histogram with a single call.
   */
  void exportHistogram(folly::StringPiece key, ExportType stat);
  void exportHistogram(folly::StringPiece key, int pct);
  template <typename... Args>
  void exportHistogram(
      folly::StringPiece key,
      ExportType stat,
      const Args&... args) {
    exportHistogram(key, stat);
    exportHistogram(key, args...);
  }
  template <typename... Args>
  void exportHistogram(folly::StringPiece key, int pct, const Args&... args) {
    exportHistogram(key, pct);
    exportHistogram(key, args...);
  }

  /*
   * Gets the quantile stat for the given key. If this is the first time calling
   * this function for this key, registers the quantile stat for given stats,
   * quantiles, and timeseries lengths. Each of these will be exported in the
   * format: key.{stat}[.slidingWindowPeriod] (e.g. MyStat.sum.60).
   *
   * To add values to the stat, call addValue on the QuantileStat returned.
   *
   * There are some helper consts in ExportType.h to make using this function
   * easier to use:
   *
   * ServiceData::get()->getQuantileStat(
   *    "foo",
   *    ExportTypeConsts::kSumCountAvg,
   *    QuantileConsts::kP95_P99_P999,
   *    SlidingWindowPeriodConsts::kOneMinTenMinHour);
   *
   * While this function is relatively fast, it is better to reuse the pointer
   * to the QuantileStat returned.
   */
  std::shared_ptr<QuantileStat> getQuantileStat(
      folly::StringPiece name,
      folly::Range<const ExportType*> stats = ExportTypeConsts::kCountAvg,
      folly::Range<const double*> quantiles = QuantileConsts::kP95_P99_P999,
      folly::Range<const size_t*> slidingWindowPeriod =
          SlidingWindowPeriodConsts::kOneMin);

  /**
   * Adds a value to the historical histograms for a given key.
   *
   * Please see the documentation for exportHistogramPercentile() and
   * addHistogram() to see how to configure histograms and their export
   * options.
   *
   * see the documenttion in common/stats/ExportedHistogramMapImpl.h for more
   * info.
   */
  void addHistogramValue(
      folly::StringPiece key,
      int64_t value,
      bool checkContains = false);

  /**
   * Adds a value to the historical histograms for a given key multiple times.
   *
   * Please see the documentation for exportHistogramPercentile() and
   * addHistogram() to see how to configure histograms and their export
   * options.
   *
   * see the documenttion in common/stats/ExportedHistogramMapImpl.h for more
   * info.
   */
  void addHistogramValueMult(
      folly::StringPiece key,
      int64_t value,
      int64_t times,
      bool checkContains = false);

  /**
   * Convenience function for adding the same value to stats and histgrams.
   * Creates AVG stat if no stat exists, but fatals if no histogram has been
   * created yet.
   *
   * NOTE: This function is deprecated and should not be used.  Histograms
   * provide a superset of the functionality in a timeseries dataset, so you
   * don't need to track duplicate data in both a histogram and a timeseries.
   * Just use addHistogramValue(), and export the desired stats from the
   * histogram rather than using a separate timeseries.
   */
  void addHistAndStatValue(
      folly::StringPiece key,
      int64_t value,
      bool checkContains = false);

  /**
   * Convenience function for adding the same value to stats and histgrams.
   * Creates AVG stat if no stat exists, but fatals if no histogram has been
   * created yet.
   *
   * NOTE: This function is deprecated and should not be used.  Histograms
   * provide a superset of the functionality in a timeseries dataset, so you
   * don't need to track duplicate data in both a histogram and a timeseries.
   * Just use addHistogramValue(), and export the desired stats from the
   * histogram rather than using a separate timeseries.
   */
  void addHistAndStatValues(
      folly::StringPiece key,
      const folly::Histogram<int64_t>& values,
      time_t now,
      int64_t sum,
      int64_t nsamples,
      bool checkContains = false);

  /*** Increments a "regular-style" flat counter (no historical stats) */
  int64_t incrementCounter(folly::StringPiece key, int64_t amount = 1);
  /*** Sets a "regular-style" flat counter (no historical stats) */
  int64_t setCounter(folly::StringPiece key, int64_t value);
  /*** Clear any flat counter with that name */
  void clearCounter(folly::StringPiece key);

  /**
   * Retrieves a counter value for given key (could be regular or dynamic)
   *
   * Throws std::invalid_argument() if the specified key does not exist.
   */
  int64_t getCounter(folly::StringPiece key) const;

  /*** Same as getCounter, but uses folly::Optional instead of throwing */
  folly::Optional<int64_t> getCounterIfExists(folly::StringPiece key) const;

  // Returns a list of all current counter keys (names)
  std::vector<std::string> getCounterKeys() const;

  /**
   * Returns the number of wrapped counters.
   *
   * NOTE: method combines the independently retrieved counts of three wrapped
   * containers and so there is no guarantee that the result ever maps to a
   * single snapshotted state of this instance.
   */
  uint64_t getNumCounters() const;

  /*** Retrieves all counters, both regular-style and dynamic counters */
  void getCounters(std::map<std::string, int64_t>& _return) const;
  std::map<std::string, int64_t> getCounters() const;

  /*** Retrieves a list of counter values (could be regular or dynamic) */
  void getSelectedCounters(
      std::map<std::string, int64_t>& _return,
      const std::vector<std::string>& keys) const;
  std::map<std::string, int64_t> getSelectedCounters(
      const std::vector<std::string>& keys) const;
  /*** Retrives counters whoose names match given reges */
  void getRegexCounters(
      std::map<std::string, int64_t>& _return,
      const std::string& regex) const;
  std::map<std::string, int64_t> getRegexCounters(
      const std::string& regex) const;
  /*** Returns true if a counter exists with the specified name */
  bool hasCounter(folly::StringPiece key) const;

  /**
   * Set an exported value.
   *
   * Creates a new value if this key doesn't already exist.
   */
  void setExportedValue(folly::StringPiece key, std::string value);
  void deleteExportedKey(folly::StringPiece key);
  void getExportedValue(std::string& _return, folly::StringPiece key) const;
  std::string getExportedValue(folly::StringPiece key) const;
  void getExportedValues(std::map<std::string, std::string>& _return) const;
  std::map<std::string, std::string> getExportedValues() const;
  void getSelectedExportedValues(
      std::map<std::string, std::string>& _return,
      const std::vector<std::string>& keys) const;
  std::map<std::string, std::string> getSelectedExportedValues(
      const std::vector<std::string>& keys) const;
  void getRegexExportedValues(
      std::map<std::string, std::string>& _return,
      const std::string& regex) const;
  std::map<std::string, std::string> getRegexExportedValues(
      const std::string& regex) const;

  void setUseOptionsAsFlags(bool useOptionsAsFlags);
  bool getUseOptionsAsFlags() const;
  void setOption(folly::StringPiece key, folly::StringPiece value);
  void setOptionThrowIfAbsent(folly::StringPiece key, folly::StringPiece value);
  static void setOptionAsFlags(
      const std::string& key,
      const std::string& value);
  static void setOptionAsFlagsThrowIfAbsent(
      const std::string& key,
      const std::string& value);
  static void setVModuleOption(
      folly::StringPiece key,
      folly::StringPiece value);
  /**
   * Get an option value.
   *
   * Throws std::invalid_argument if no option with this name exists.
   */
  std::string getOption(folly::StringPiece key) const;
  void getOptions(std::map<std::string, std::string>& _return) const;
  std::map<std::string, std::string> getOptions() const;

  using DynamicOptionGetter = folly::Function<std::string()>;
  using DynamicOptionSetter = folly::Function<void(std::string const&)>;

  /**
   * Register dynamic callbacks that will be called when getOption()
   * and setOption() is invoked for the specified name.
   *
   * This will override any existing dynamic option or static option with the
   * specified name.
   */
  void registerDynamicOption(
      folly::StringPiece name,
      DynamicOptionGetter getter,
      DynamicOptionSetter setter);

  bool containsStatsFor(const std::string& name) {
    return statsMap_.contains(name);
  }

 private:
  struct Counter : std::atomic<int64_t> {
    Counter() : std::atomic<int64_t>{0} {}
    Counter(Counter&& other) noexcept
        : std::atomic<int64_t>{other.load(std::memory_order_relaxed)} {}
  };
  template <typename Mapped>
  using StringKeyedMap = folly::F14FastMap<std::string, Mapped>;

  const std::chrono::seconds aliveSince_;

  std::atomic<bool> useOptionsAsFlags_;
  folly::Synchronized<StringKeyedMap<std::string>> options_;

  folly::Synchronized<StringKeyedMap<Counter>> counters_;
  folly::Synchronized<StringKeyedMap<folly::Synchronized<std::string>>>
      exportedValues_;

  DynamicCounters dynamicCounters_;
  DynamicStrings dynamicStrings_;
  ExportedStatMapImpl statsMap_;
  fb303::detail::QuantileStatMap quantileMap_;
  ExportedHistogramMapImpl histMap_;
  void mergeOptionsWithGflags(
      std::map<std::string, std::string>& _return) const;

  class DynamicOption {
   public:
    DynamicOption() {}
    DynamicOption(DynamicOptionGetter g, DynamicOptionSetter s)
        : getter(std::move(g)), setter(std::move(s)) {}

    DynamicOptionGetter getter;
    DynamicOptionSetter setter;
  };
  folly::Synchronized<StringKeyedMap<DynamicOption>> dynamicOptions_;
};

// A "pseudo-pointer" to the ServiceData singleton.
// Use it like you would use a pointer to the singleton.
// This is instantiated in a global var called "fbData", so you
// can just say "fbData->..." as usual.
// (This is for historical purposes, when fbData was a simple pointer to
// a single global instance, before we had so much trouble with SIOF.)
struct ServiceDataWrapper {
  /*
   * This silly little struct should have no internal state, lest it fall
   * victim to the static-initialization order fiasco, or SIOF:
   * https://isocpp.org/wiki/faq/ctors#static-init-order
   * If it has no internal state, then it's ok to use no matter what its
   * initialization order is, because since it's stateless, there's nothing
   * to initialize.
   */

  /** provides ptr to the singleton ServiceData. */
  ServiceData* operator->() const {
    DCHECK(*this) << "fbData used after singleton destruction; fix callsite "
                     "to use `if (fbData)` to check beforehand or see notes "
                     "at fburl.com/fbdata-fiascos";
    return ServiceData::get();
  }

  ServiceData* ptr() const {
    return ServiceData::get();
  }

  /**
   * Returns true iff Singleton hasn't yet been destroyed.
   * Should use this prior to trying to access the singleton
   * with "get()" but note there's a possible race when doing
   * so (off-chance the Singleton might go away between this call and "ptr()").
   */
  /* implicit */
  operator bool() const {
    return true;
  }

  /**
   * The only safe way to access ServiceData singleton. Other approaches end up
   * racing against the singleton destructor.
   */
  std::shared_ptr<ServiceData> try_get() const {
    return ServiceData::getShared();
  }
};

/**
 * Provides a pointer to the global ServiceData singleton.
 *
 * This is provided as an easier version of facebook::stats::ServiceData::get().
 * Can be used from static initializers; safely accesses the single ServiceData
 * instance using its singleton (see `ServiceData::get()`).
 */
static constexpr ServiceDataWrapper fbData;

} // namespace fb303
} // namespace facebook
