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

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <variant>

#include <fb303/ExportType.h>
#include <fb303/SimpleLRUMap.h>
#include <fb303/ThreadLocalStatsMap.h>
#include <folly/Format.h>
#include <folly/MapUtil.h>
#include <folly/Overload.h>
#include <folly/Range.h>
#include <folly/Synchronized.h>
#include <folly/ThreadLocal.h>
#include <folly/container/F14Map.h>
#include <folly/experimental/FunctionScheduler.h>
#include <folly/hash/Hash.h>
#include <folly/synchronization/CallOnce.h>

namespace facebook {
namespace fb303 {

/**
 * ThreadCachedServiceData wraps a ServiceData object, and exposes the same
 * API, but uses a ThreadLocalStatsMap to make statistic update operations
 * more efficient.
 *
 * It does require periodically calling publishStats() in order to flush the
 * stats data cached in each thread to the underlying ServiceData object.  The
 * startPublishThread() can be used to start a new thread that will
 * periodically call publishStats().
 *
 *
 * ThreadCachedServiceData is designed to be a drop-in replacement for
 * ServiceData for places in your code that need to update stats but want a
 * higher-performance implementation.  It implements nearly the full
 * ServiceData API in case you want to completely replace all ServiceData uses
 * in your code with a ThreadCachedServiceData object.
 */
class ThreadCachedServiceData {
 public:
  using ThreadLocalStatsMap = ThreadLocalStatsMapT<TLStatsThreadSafe>;
  using StatsThreadLocal =
      folly::ThreadLocal<ThreadLocalStatsMap, ThreadCachedServiceData>;

  /**
   *  Get the thread-local ThreadCachedStatsMap object.
   */
  static StatsThreadLocal& getStatsThreadLocal();

  /**
   * Get the ServiceData object that this ThreadCachedServiceData object wraps.
   */
  ServiceData* getServiceData() const {
    return serviceData_;
  }

  /**
   * Get a shared pointer to the singleton ThreadCachedServiceData object.
   * Deprecated; use `get()` instead
   */
  static std::shared_ptr<ThreadCachedServiceData> getShared();

  /**
   * Get a pointer to the singleton ThreadCachedServiceData object.
   */
  static ThreadCachedServiceData* get();

  /*
   * Flush all of the statistics cached in each thread into the main
   * ServiceData object.
   */
  void publishStats();

  /**
   * Start a separate thread to periodically call publishStats()
   * at the specified interval.
   *
   * If interval is zero or negative, startPublishThread() will start the
   * publish thread with an interval of 1 second if it is not already running.
   * If it is already running, startPublishThread() will return without
   * changing the interval.  This is intended to be used by library code, since
   * the library does not know if the main program has called
   * startPublishThread() already or not.  The library can call
   * startPublishThread(milliseconds(-1)) to ensure that the publish thread is
   * running, without overriding the publish interval in case the main thread
   * has called startPublishThread() with its own interval setting.
   *
   * If the interval is positive and the publish thread is already running, it
   * is rescheduled to use the new interval setting.
   */
  void startPublishThread(std::chrono::milliseconds interval);

  /**
   * Stop the separate aggregation thread, if it is running.
   */
  void stopPublishThread();

  /**
   * Is the aggregation thread currently running?
   */
  bool publishThreadRunning() const;

  /*
   * Functions to update stats.
   */

  void addStatValue(folly::StringPiece key, int64_t value = 1) {
    getThreadStats()->addStatValue(key, value);
  }

  /** Small LRU lookup set class, for exported keys. */
  struct ExportKeyCache
      : public SimpleLRUMap<std::string, bool, folly::F14FastMap> {
    /**
     * It would be best if this can be overridden by gflag; but it's often the
     * case that the stats classes are used pre-startup / post-shutdown and thus
     * trying to access gflags might stir up an initialization/shutdown order
     * fiasco.  So this constant is hardcoded here.
     */
    static constexpr size_t kLRUMaxSize = 1000;

    ExportKeyCache() : SimpleLRUMap(kLRUMaxSize) {}

    void add(folly::StringPiece key) {
      set(key, true);
    }

    bool has(folly::StringPiece key) {
      return find(key, /*moveToFront*/ true) != end();
    }
  };

  void
  addStatValue(folly::StringPiece key, int64_t value, ExportType exportType);
  void clearStat(folly::StringPiece key, ExportType exportType);

  void addStatValueAggregated(
      folly::StringPiece key,
      int64_t sum,
      int64_t numSamples) {
    getThreadStats()->addStatValueAggregated(key, sum, numSamples);
  }

  void addHistogramValue(folly::StringPiece key, int64_t value) {
    getThreadStats()->addHistogramValue(key, value);
  }

  /*
   * Note that unlike ServiceData::incrementCounter(),
   * ThreadCachedServiceData::incrementCounter() does not return a value:
   * the counter value cannot be returned without accessing the global state
   * (and performing the required locking).
   *
   * Most users only care about performing a fast update, and do not care about
   * the returned value.  Therefore we opt to simply not return a value here.
   */
  void incrementCounter(folly::StringPiece key, int64_t amount = 1) {
    getThreadStats()->incrementCounter(key, amount);
  }

  int64_t setCounter(folly::StringPiece key, int64_t value);
  void clearCounter(folly::StringPiece key);

  void zeroStats();

  /*
   * Deprecated functions for updating both a histogram and a timeseries
   * with the same data.
   *
   * Don't use these: a histogram can export the same stats as a timeseries,
   * so there is no need to also track the same data in a timeseries object.
   */
  [[deprecated]] void addHistAndStatValue(
      folly::StringPiece key,
      int64_t value,
      bool checkContains = false);
  [[deprecated]] void addHistAndStatValues(
      folly::StringPiece key,
      const folly::Histogram<int64_t>& values,
      time_t now,
      int64_t sum,
      int64_t nsamples,
      bool checkContains = false);

  /*
   * Wrapper functions around ServiceData methods
   */

  std::chrono::seconds getAliveSince() const {
    return getServiceData()->getAliveSince();
  }
  ExportedStatMapImpl* getStatMap() {
    return getServiceData()->getStatMap();
  }
  ExportedHistogramMapImpl* getHistogramMap() {
    return getServiceData()->getHistogramMap();
  }
  DynamicCounters* getDynamicCounters() {
    return getServiceData()->getDynamicCounters();
  }
  DynamicStrings* getDynamicStrings() {
    return getServiceData()->getDynamicStrings();
  }

  void addStatExportType(
      folly::StringPiece key,
      ExportType exportType = ExportType::AVG,
      const ExportedStat* statPrototype = nullptr) {
    getServiceData()->addStatExportType(key, exportType, statPrototype);
  }
  void addStatExports(
      folly::StringPiece key,
      folly::StringPiece stats,
      int64_t bucketWidth = 0,
      int64_t min = 0,
      int64_t max = 0,
      const ExportedStat* statPrototype = nullptr) {
    getServiceData()->addStatExports(
        key, stats, bucketWidth, min, max, statPrototype);
  }
  bool addHistogram(
      folly::StringPiece key,
      int64_t bucketWidth,
      int64_t min,
      int64_t max) {
    return getServiceData()->addHistogram(key, bucketWidth, min, max);
  }
  bool addHistogram(
      folly::StringPiece key,
      const ExportedHistogram& prototype) {
    return getServiceData()->addHistogram(key, prototype);
  }
  void exportHistogramPercentile(folly::StringPiece key, int pct) {
    getServiceData()->exportHistogramPercentile(key, pct);
  }
  template <typename... Args>
  void exportHistogramPercentile(
      folly::StringPiece key,
      int pct,
      const Args&... args) {
    getServiceData()->exportHistogramPercentile(key, pct, args...);
  }
  template <typename... Args>
  void exportHistogram(folly::StringPiece key, const Args&... args) {
    getServiceData()->exportHistogram(key, args...);
  }

  int64_t getCounter(folly::StringPiece key) const {
    return getServiceData()->getCounter(key);
  }
  void getCounters(std::map<std::string, int64_t>& _return) const {
    getServiceData()->getCounters(_return);
  }
  void getSelectedCounters(
      std::map<std::string, int64_t>& _return,
      const std::vector<std::string>& keys) const {
    getServiceData()->getSelectedCounters(_return, keys);
  }
  bool hasCounter(folly::StringPiece key) const {
    return getServiceData()->hasCounter(key);
  }

  void setExportedValue(folly::StringPiece key, std::string value) {
    getServiceData()->setExportedValue(key, std::move(value));
  }
  void getExportedValue(std::string& _return, folly::StringPiece key) {
    getServiceData()->getExportedValue(_return, key);
  }
  void getExportedValues(std::map<std::string, std::string>& _return) {
    getServiceData()->getExportedValues(_return);
  }
  void getSelectedExportedValues(
      std::map<std::string, std::string>& _return,
      const std::vector<std::string>& keys) {
    getServiceData()->getSelectedExportedValues(_return, keys);
  }

  void setUseOptionsAsFlags(bool useOptionsAsFlags) {
    getServiceData()->setUseOptionsAsFlags(useOptionsAsFlags);
  }
  bool getUseOptionsAsFlags() const {
    return getServiceData()->getUseOptionsAsFlags();
  }
  void setOption(folly::StringPiece key, folly::StringPiece value) {
    getServiceData()->setOption(key, value);
  }
  static void setVModuleOption(std::string_view key, std::string_view value) {
    ServiceData::setVModuleOption(key, value);
  }
  std::string getOption(folly::StringPiece key) const {
    return getServiceData()->getOption(key);
  }
  void getOptions(std::map<std::string, std::string>& _return) const {
    getServiceData()->getOptions(_return);
  }

  typedef ServiceData::DynamicOptionGetter DynamicOptionGetter;
  typedef ServiceData::DynamicOptionSetter DynamicOptionSetter;

  void registerDynamicOption(
      folly::StringPiece name,
      DynamicOptionGetter getter,
      DynamicOptionSetter setter) {
    getServiceData()->registerDynamicOption(
        name, std::move(getter), std::move(setter));
  }

  typedef ThreadLocalStatsMap::TLCounter TLCounter;
  typedef ThreadLocalStatsMap::TLHistogram TLHistogram;
  typedef ThreadLocalStatsMap::TLTimeseries TLTimeseries;

  /**
   * Get a pointer to the ThreadLocalStatsMap object that caches stats for this
   * thread.
   *
   * If no ThreadLocalStatsMap exists yet for this thread, a new one will be
   * created and returned.
   */
  ThreadLocalStatsMap* getThreadStats() {
    return threadLocalStats_->get();
  }

  std::chrono::milliseconds getPublisherInterval() const {
    return interval_.load(std::memory_order_relaxed);
  }

 private:
  ThreadCachedServiceData();
  ThreadCachedServiceData(const ThreadCachedServiceData&) = delete;
  ThreadCachedServiceData& operator=(const ThreadCachedServiceData&) = delete;
  static ThreadCachedServiceData& getInternal();

  friend class PublisherManager;

  ServiceData* serviceData_;
  StatsThreadLocal* threadLocalStats_;
  using KeyCacheTable =
      std::array<ExportKeyCache, ExportTypeMeta::kNumExportTypes>;
  folly::ThreadLocal<KeyCacheTable> keyCacheTable_;

  std::atomic<std::chrono::milliseconds> interval_{
      std::chrono::milliseconds(0)};
};

struct TLMinuteOnlyTimeseries : public ThreadCachedServiceData::TLTimeseries {
  using TLTimeseries = ThreadCachedServiceData::TLTimeseries;
  template <typename... ExportTypes>
  explicit TLMinuteOnlyTimeseries(folly::StringPiece name, ExportTypes... types)
      : TLTimeseries(
            ThreadCachedServiceData::get()->getThreadStats(),
            name,
            (size_t)60,
            (size_t)1,
            facebook::fb303::kMinuteOnlyDurations,
            types...) {}
};

/**
 * Prevents implicit conversions.
 */
template <typename Bool, std::enable_if_t<std::is_same_v<Bool, bool>, int> = 0>
constexpr std::string_view dynamic_key(Bool value) {
  return !value ? "false" : "true";
}

namespace internal {

struct HistogramSpec {
  int64_t bucketWidth;
  int64_t min;
  int64_t max;
  std::vector<ExportType> stats;
  std::vector<int> percentiles;
  MultiLevelTimeSeries<CounterType> levels;

  template <typename... Args>
  HistogramSpec(
      int64_t bucketWidth_,
      int64_t min_,
      int64_t max_,
      const Args&... args)
      : bucketWidth(bucketWidth_),
        min(min_),
        max(max_),
        levels(MinuteTenMinuteHourTimeSeries<CounterType>()) {
    ctorHandleArgs(args...);
  }

  template <typename ServiceData>
  void apply(folly::StringPiece key, ServiceData* sd) const {
    ExportedHistogram prototype(bucketWidth, min, max, levels);
    sd->addHistogram(key, prototype);
    for (const auto stat : stats) {
      sd->exportHistogram(key, stat);
    }
    for (const auto pctile : percentiles) {
      sd->exportHistogramPercentile(key, pctile);
    }
  }

 private:
  void ctorHandleArg(ExportType stat) {
    this->stats.push_back(stat);
  }

  void ctorHandleArg(int pctile) {
    this->percentiles.push_back(pctile);
  }

  void ctorHandleArg(const MultiLevelTimeSeries<CounterType>& levels_) {
    this->levels = levels_;
  }

  template <typename... Args>
  void ctorHandleArgs(const Args&... args) {
    int _[] = {(ctorHandleArg(args), 0)...};
    (void)_;
  }
};

template <size_t N>
class FormattedKeyHolder {
 public:
  using Subkey = std::variant<int64_t, std::string>;
  using SubkeyArray = std::array<Subkey, N>;

  // Sanity check that our set of arguments contains only integers or strings.
  // This provides better type-safety because otherwise we could implicitly
  // round down floating-point keys or get a runtime-error with nullptrs.
  template <typename T>
  static constexpr bool IsValidSubkey =
      (std::is_convertible_v<T, folly::StringPiece> ||
       std::is_integral_v<T>)&&!std::is_same_v<T, std::nullptr_t> &&
      !std::is_same_v<T, bool>;

  // Hash function
  struct SubkeyHash //
      : private folly::hasher<int64_t>,
        private folly::hasher<std::string>,
        private folly::hasher<std::string_view> {
    using is_transparent = void;
    using folly_is_avalanching = folly::Conjunction<
        folly::hasher<int64_t>::folly_is_avalanching,
        folly::hasher<std::string>::folly_is_avalanching,
        folly::hasher<std::string_view>::folly_is_avalanching>;

    using folly::hasher<int64_t>::operator();
    using folly::hasher<std::string>::operator();
    using folly::hasher<std::string_view>::operator();

    size_t operator()(const Subkey& v) const {
      return std::visit(*this, v);
    }
  };

  class SubkeyArrayHash : private SubkeyHash {
   public:
    using is_transparent = void;
    using folly_is_avalanching = typename SubkeyHash::folly_is_avalanching;

    size_t operator()(const SubkeyArray& v) const {
      return hash(v, std::make_index_sequence<N>{});
    }
    template <typename... A, std::enable_if_t<sizeof...(A) == N, int> = 0>
    size_t operator()(const std::tuple<A...>& v) const {
      return hash(v, std::make_index_sequence<N>{});
    }

   private:
    template <typename O, size_t... I>
    size_t hash(const O& v, std::index_sequence<I...>) const {
      using std::get;
      auto& base = static_cast<SubkeyHash const&>(*this);
      return folly_is_avalanching::value
          ? (0 ^ ... ^ base(get<I>(v)))
          : folly::hash::hash_combine_generic(base, get<I>(v)...);
    }
  };

  class SubkeyArrayEqualTo {
   public:
    using is_transparent = void;

    bool operator()(const SubkeyArray& a, const SubkeyArray& b) const {
      return a == b;
    }
    template <typename... A, std::enable_if_t<sizeof...(A) == N, int> = 0>
    bool operator()(const std::tuple<A...>& a, const std::tuple<A...>& b)
        const {
      return a == b;
    }
    template <typename... A, std::enable_if_t<sizeof...(A) == N, int> = 0>
    bool operator()(const std::tuple<A...>& a, const SubkeyArray& b) const {
      return eq(a, b, std::make_index_sequence<N>{});
    }
    template <typename... A, std::enable_if_t<sizeof...(A) == N, int> = 0>
    bool operator()(const SubkeyArray& a, const std::tuple<A...>& b) const {
      return eq(b, a, std::make_index_sequence<N>{});
    }

   private:
    template <typename... A, size_t... I>
    static bool eq(
        const std::tuple<A...>& a,
        const SubkeyArray& b,
        std::index_sequence<I...>) {
      return (true && ... && eq(std::get<I>(a), b[I]));
    }
    template <typename T, typename A>
    static bool eq(A a, const Subkey& b) {
      return std::holds_alternative<T>(b) && a == std::get<T>(b);
    }
    static bool eq(int64_t a, const Subkey& b) {
      return eq<int64_t>(a, b);
    }
    static bool eq(std::string_view a, const Subkey& b) {
      return eq<std::string>(a, b);
    }
  };

  // The global map maps subkeys to formatted keys and also remembers
  // which formatted keys have already been registered with ServiceData.
  using GlobalMap = folly::F14NodeMap< //
      SubkeyArray,
      std::string,
      SubkeyArrayHash,
      SubkeyArrayEqualTo>;

  // The local map is a thread-local cache of the global map.
  // This helps avoid contention as we do not have to acquire a global read
  // lock in order to update the stats.
  // To reduce memory footprint, the values in the cache are pointers to the
  // values in globalMap_.
  // (we use const string pointers instead of StringPieces because
  //  ThreadCachedServiceData::addStatValue() only accepts string references)
  using LocalMap = folly::F14FastMap<
      std::reference_wrapper<const SubkeyArray>,
      const std::string*,
      SubkeyArrayHash,
      SubkeyArrayEqualTo>;

  // Takes a key format which may have (e.g. "foo.{}") containing one or
  // more special placeholders "{}" that can be replaced with a subkey.
  // Also takes a callback used to prepare a new key for first use, where
  // callback invocations are guaranteed to be mutually excluded. An example
  // of preparation for first use is to register key with the ServiceData
  // singleton.
  FormattedKeyHolder(
      std::string keyFormat,
      std::function<void(const std::string&)> prepareKey)
      : keyFormat_(std::move(keyFormat)), prepareKey_(std::move(prepareKey)) {}

  // Returns a copy of globalMap_.
  // Only for debugging; not designed to be efficient.
  GlobalMap getMap() const {
    return globalMap_.copy();
  }

  /**
   * Given a subkey (e.g. "bar"), returns the full stat key (e.g. "foo.bar")
   * and registers the stats export types if not registered already.
   */
  template <typename... Args>
  const std::string& getFormattedKey(Args&&... subkeys) {
    static_assert(sizeof...(Args) == N, "Incorrect number of subkeys.");
    static_assert(
        (IsValidSubkey<folly::remove_cvref_t<Args>> && ...),
        "Arguments must be strings or integers");

    auto decay = folly::overload(decay_<int64_t>{}, decay_<std::string_view>{});
    // calling outline folly::get_default would be a small perf hit
    auto const& map = *localMap_; // so call map-find inline to avoid
    auto it = map.find(std::tuple{decay(subkeys)...});
    return FOLLY_LIKELY(it != map.end())
        ? *it->second
        : getFormattedKeySlow(std::forward<Args>(subkeys)...);
  }

  template <typename... Args>
  void eraseFormattedKey(Args&&... subkeys) {
    static_assert(sizeof...(Args) == N, "Incorrect number of subkeys.");
    static_assert(
        (IsValidSubkey<folly::remove_cvref_t<Args>> && ...),
        "Arguments must be strings or integers");

    auto decay = folly::overload(decay_<int64_t>{}, decay_<std::string_view>{});
    // calling outline folly::get_default would be a small perf hit
    auto& map = *localMap_; // so call map-find inline to avoid
    map.erase(std::tuple{decay(subkeys)...});
  }

 private:
  template <typename T>
  struct decay_ {
    FOLLY_ERASE constexpr T operator()(T const& t) const {
      return t;
    }
  };
  template <typename T, typename S = T>
  struct mkvar_ {
    FOLLY_ERASE Subkey operator()(T const& t) const {
      return Subkey(std::in_place_type<S>, t);
    }
  };

  /**
   * Assumes a subkey-array is not in the local map. Gets it from the global
   * map, inserting if necessary, inserts it into the local map, and returns the
   * corresponding formatted-key.
   */
  template <typename... Args>
  const std::string& getFormattedKeySlow(Args&&... subkeys) {
    auto mkvar = folly::overload(
        mkvar_<int64_t>{}, mkvar_<std::string_view, std::string>{});
    auto const& [k, v] = getFormattedKeyGlobal({mkvar(subkeys)...});
    return *localMap_->emplace(std::cref(k), &v).first->second;
  }

  /**
   * Returns a pair of (subkey-array, formatted-key) by-ref
   * e.g. (("foo", 42), "my_counter.foo.42")
   * and registers the stats export types if not registered already.
   * We return both the subkey and the stats key so that getStats() can build
   * its threadlocal map of subkey-to-formatted-key without copying the strings.
   */
  std::pair<
      const typename GlobalMap::key_type&,
      const typename GlobalMap::mapped_type&>
  getFormattedKeyGlobal(const SubkeyArray& subkeyArray) {
    typedef std::pair<
        const typename GlobalMap::key_type&,
        const typename GlobalMap::mapped_type&>
        ReturnType;

    // Try looking up the subkeyArray in our global stats map
    {
      auto readPtr = globalMap_.rlock();
      const auto iter = readPtr->find(subkeyArray);
      if (iter != readPtr->end()) {
        return ReturnType(iter->first, iter->second);
      }
    }

    // We did not find the key in our global stats map.
    // Upgrade our lock and try again.
    auto upgradePtr = globalMap_.ulock();
    auto iter = upgradePtr->find(subkeyArray);
    if (iter != upgradePtr->end()) {
      // Another thread has updated the global stats map for us.
      return ReturnType(iter->first, iter->second);
    }

    // We still did not find it.
    // Create a formatted key and switch to a writer-lock and update the
    // global stats map.
    std::array<std::string, N> subkeyStrings;
    for (size_t i = 0; i < N; ++i) {
      subkeyStrings[i] = folly::variant_match(
          subkeyArray[i],
          [](int64_t v) { return std::to_string(v); },
          [](std::string const& v) { return v; });
    }
    auto formattedKey = folly::svformat(keyFormat_, subkeyStrings);
    prepareKey_(formattedKey);

    auto writePtr = upgradePtr.moveFromUpgradeToWrite();
    iter = writePtr->emplace(subkeyArray, std::move(formattedKey)).first;
    return ReturnType(iter->first, iter->second);
  }

 private:
  std::string keyFormat_;
  std::function<void(const std::string& key)> prepareKey_;
  folly::Synchronized<GlobalMap> globalMap_;
  folly::ThreadLocal<LocalMap> localMap_;
};

} // namespace internal

/**
 * Wrapper intended for setting up a TLStatT object in an explicitly
 * thread local manner.  Underlying TLStatT object is lazily instantiated
 * on first use.  Nothing outside this file should ever try and use this
 * abstract class directly.
 */
template <class TLStatT>
class StatWrapperBase {
 public:
  explicit StatWrapperBase(std::string key) : key_(std::move(key)) {}
  explicit StatWrapperBase(std::string_view key) : key_(key) {}
  virtual ~StatWrapperBase() = default;

  // Even though the destructor above is defaulted, and required as this
  // class is used as a base class, it prevents the implicit generation
  // of move constructors so we declare them explicitly.  It should be
  // safe to default because we don't actually have any special
  // destruction logic.
  StatWrapperBase(StatWrapperBase&&) = default;
  StatWrapperBase& operator=(StatWrapperBase&&) = default;

  const std::string& name() const {
    return key_;
  }

  // Accessor does not guarantee underlying stat object has been initialized.
  TLStatT* getTcStatUnsafe() const {
    return tlStat_->get();
  }

 protected:
  std::string key_;

  folly::ThreadLocal<std::shared_ptr<TLStatT>> tlStat_;

  virtual std::shared_ptr<TLStatT> getStatSafe(const std::string& key) = 0;

  TLStatT* tcStat() {
    TLStatT* cached = tlStat_->get();
    if (cached) {
      return cached;
    }

    auto tlStat = getStatSafe(key_);
    *tlStat_ = tlStat;
    return tlStat.get();
  }
};

class CounterWrapper
    : public StatWrapperBase<ThreadCachedServiceData::TLCounter> {
 public:
  // Two constructors are provided for use in the DEFINE_counter macro below.
  // If no name is provided, then first constructor is used with the variable
  // name being the key. If a name is provided, the second constructor is used.
  explicit CounterWrapper(std::string key) : StatWrapperBase(std::move(key)) {}
  CounterWrapper(std::string_view /*var*/, std::string_view key)
      : StatWrapperBase(key) {}
  CounterWrapper(const char* /*var*/, std::string_view key)
      : StatWrapperBase(key) {}

  void incrementValue(CounterType amount = 1) {
    tcStat()->incrementValue(amount);
  }

 protected:
  std::shared_ptr<ThreadCachedServiceData::TLCounter> getStatSafe(
      const std::string& key) override {
    return ThreadCachedServiceData::getStatsThreadLocal()->getCounterSafe(key);
  }
};

// TODO: deprecate this class once Zeus fixes their destruction ordering bug
// https://fburl.com/4a082hxc and rename TimeseriesPolymorphicWrapper at that
// time to TimeseriesWrapper.  Luckily no one else but Proxygen should be
// using TimeseriesPolymorphicWrapper and it is a one line change for us.
class TimeseriesWrapper {
 public:
  // Multiple forms of the constructor. We require a first string, but take an
  // optional second string. If a second string is passed, that is the key and
  // the first string is unused; otherwise, the first string is the key. This
  // supports the short form of DEFINE_dynamic_timeseries, which reuses the var
  // name as the stat name, and the long form of the macro, which allows the
  // caller to customize the stat name instead of reusing the var name.
  template <
      typename Arg1,
      typename... Args,
      typename std::enable_if<
          std::is_convertible<Arg1, std::string_view>::value>::type* = nullptr>
  TimeseriesWrapper(
      const std::string& /*varname*/,
      const Arg1& key,
      const Args&... args)
      : key_(key) {
    exportStats(nullptr, args...);
  }
  template <
      typename Arg1,
      typename... Args,
      typename std::enable_if<
          std::is_convertible<Arg1, ExportedStat>::value>::type* = nullptr,
      typename std::enable_if<!std::is_lvalue_reference<Arg1>::value>::type* =
          nullptr>
  TimeseriesWrapper(
      const std::string& varname,
      Arg1&& prototype,
      const Args&... args)
      : key_(varname) {
    exportStats(&prototype, args...);
  }
  template <
      typename Arg1,
      typename... Args,
      typename std::enable_if<
          !std::is_convertible<Arg1, std::string_view>::value>::type* = nullptr,
      typename std::enable_if<
          !std::is_convertible<Arg1, ExportedStat>::value>::type* = nullptr>
  TimeseriesWrapper(
      const std::string& varname,
      const Arg1& arg1,
      const Args&... args)
      : key_(varname) {
    exportStats(nullptr, arg1, args...);
  }
  template <typename... Args>
  TimeseriesWrapper(const std::string& key, const Args&... args) : key_(key) {
    exportStats(nullptr, args...);
  }

  void add(int64_t value = 1) {
    tcTimeseries()->addValue(value);
  }

  void addAggregated(int64_t sum, int64_t numSamples) {
    tcTimeseries()->addValueAggregated(sum, numSamples);
  }

 private:
  std::string key_;

  folly::ThreadLocal<std::shared_ptr<ThreadCachedServiceData::TLTimeseries>>
      tlTimeseries_;

  inline ThreadCachedServiceData::TLTimeseries* tcTimeseries() {
    ThreadCachedServiceData::TLTimeseries* cached = tlTimeseries_->get();
    if (cached) {
      return cached;
    }

    auto timeseries =
        ThreadCachedServiceData::getStatsThreadLocal()->getTimeseriesSafe(key_);
    *tlTimeseries_ = timeseries;
    return timeseries.get();
  }

  template <typename... Args>
  void exportStats(const ExportedStat* statPrototype, const Args&... args) {
    int _[] = {
        (ServiceData::get()->addStatExportType(key_, args, statPrototype),
         0)...};
    (void)_;
  }
};

// Abstract TimeseriesWrapper base class that implements the public
// interface that callers should utilize for interacting with the underlying
// stat.
class TimeseriesWrapperBase
    : public StatWrapperBase<ThreadCachedServiceData::TLTimeseries> {
 public:
  explicit TimeseriesWrapperBase(const std::string& key)
      : StatWrapperBase(key) {}

  void add(int64_t value = 1) {
    tcStat()->addValue(value);
  }

  void addAggregated(int64_t sum, int64_t numSamples) {
    tcStat()->addValueAggregated(sum, numSamples);
  }
};

// TODO: rename to TimeseriesWrapper once https://fburl.com/4a082hxc is
// solved.  For others, do not use this class but instead use
// TimeseriesWrapper and when the switch happens it should be a no-op
// for everyone except proxygen.
class TimeseriesPolymorphicWrapper : public TimeseriesWrapperBase {
 public:
  // Multiple forms of the constructor. We require a first string, but take an
  // optional second string. If a second string is passed, that is the key and
  // the first string is unused; otherwise, the first string is the key. This
  // supports the short form of DEFINE_dynamic_timeseries, which reuses the var
  // name as the stat name, and the long form of the macro, which allows the
  // caller to customize the stat name instead of reusing the var name.
  template <
      typename Arg1,
      typename... Args,
      typename std::enable_if<
          std::is_convertible<Arg1, std::string_view>::value>::type* = nullptr>
  TimeseriesPolymorphicWrapper(
      const std::string& /*varname*/,
      const Arg1& key,
      const Args&... args)
      : TimeseriesWrapperBase(key) {
    exportStats(args...);
  }
  template <
      typename Arg1,
      typename... Args,
      typename std::enable_if<
          !std::is_convertible<Arg1, std::string_view>::value>::type* = nullptr>
  TimeseriesPolymorphicWrapper(
      const std::string& varname,
      const Arg1& arg1,
      const Args&... args)
      : TimeseriesWrapperBase(varname) {
    exportStats(arg1, args...);
  }
  template <typename... Args>
  explicit TimeseriesPolymorphicWrapper(
      const std::string& key,
      const Args&... args)
      : TimeseriesWrapperBase(key) {
    exportStats(args...);
  }

 protected:
  std::shared_ptr<ThreadCachedServiceData::TLTimeseries> getStatSafe(
      const std::string& key) override {
    return ThreadCachedServiceData::getStatsThreadLocal()->getTimeseriesSafe(
        key);
  }

 private:
  template <typename... Args>
  void exportStats(const Args&... args) {
    // Created counters will export with fb303::kMinuteTenMinuteHourDurations
    // levels.
    int _[] = {(ServiceData::get()->addStatExportType(key_, args), 0)...};
    (void)_;
  }
};

struct MinuteTimeseriesWrapper : public TimeseriesWrapperBase {
 public:
  template <typename... Args>
  explicit MinuteTimeseriesWrapper(std::string key, const Args&... args)
      : TimeseriesWrapperBase(std::move(key)) {
    exportStats(args...);
  }

 protected:
  std::shared_ptr<ThreadCachedServiceData::TLTimeseries> getStatSafe(
      const std::string& key) override {
    return ThreadCachedServiceData::getStatsThreadLocal()->getTimeseriesSafe(
        key, 60u, 2u, fb303::kMinuteDurations);
  }

 private:
  template <typename... Args>
  void exportStats(const Args&... args) {
    // Created counters will export with fb303::kMinuteDurations
    // levels.
    int _[] = {
        (ServiceData::get()->addStatExportType(
             key_, args, &templateExportedStat()),
         0)...};
    (void)_;
  }

  static const ExportedStat& templateExportedStat();
};

class QuarterMinuteOnlyTimeseriesWrapper : public TimeseriesWrapperBase {
 public:
  template <typename... Args>
  explicit QuarterMinuteOnlyTimeseriesWrapper(
      std::string key,
      const Args&... args)
      : TimeseriesWrapperBase(std::move(key)) {
    exportStats(args...);
  }

 protected:
  std::shared_ptr<ThreadCachedServiceData::TLTimeseries> getStatSafe(
      const std::string& key) override {
    return ThreadCachedServiceData::getStatsThreadLocal()->getTimeseriesSafe(
        key, 15u, 1u, fb303::kQuarterMinuteOnlyDurations);
  }

 private:
  template <typename... Args>
  void exportStats(const Args&... args) {
    // Created counters will export with fb303::kQuarterMinuteOnlyDurations
    // levels.
    int _[] = {
        (ServiceData::get()->addStatExportType(
             key_, args, &templateExportedStat()),
         0)...};
    (void)_;
  }

  static const ExportedStat& templateExportedStat();
};

struct MinuteOnlyTimeseriesWrapper : public TimeseriesWrapperBase {
 public:
  template <typename... Args>
  explicit MinuteOnlyTimeseriesWrapper(std::string key, const Args&... args)
      : TimeseriesWrapperBase(std::move(key)) {
    exportStats(args...);
  }

 protected:
  std::shared_ptr<ThreadCachedServiceData::TLTimeseries> getStatSafe(
      const std::string& key) override {
    return ThreadCachedServiceData::getStatsThreadLocal()->getTimeseriesSafe(
        key, (size_t)60, (size_t)1, fb303::kMinuteOnlyDurations);
  }

 private:
  template <typename... Args>
  void exportStats(const Args&... args) {
    // Created counters will export with fb303::kMinuteOnlyDurations
    // levels.
    int _[] = {
        (ServiceData::get()->addStatExportType(
             key_, args, &templateExportedStat()),
         0)...};
    (void)_;
  }

  static const ExportedStat& templateExportedStat();
};

class HistogramWrapper {
 public:
  template <typename... Args>
  HistogramWrapper(
      const std::string& /*varname*/,
      const std::string& key,
      int64_t bucketWidth,
      int64_t min,
      int64_t max,
      const Args&... args)
      : HistogramWrapper(key, bucketWidth, min, max, args...) {}

  template <typename... Args>
  HistogramWrapper(
      const std::string& key,
      int64_t bucketWidth,
      int64_t min,
      int64_t max,
      const Args&... args)
      : key_(key), spec_(bucketWidth, min, max, args...) {}

  void add(int64_t value = 1) {
    ensureApplySpec();
    (*ThreadCachedServiceData::getStatsThreadLocal())
        .addHistogramValue(key_, value);
  }

 protected:
  void doApplySpecLocked() {
    spec_.apply(key_, ThreadCachedServiceData::get());
  }
  FOLLY_ALWAYS_INLINE void ensureApplySpec() {
    // minimize inline slow path size by passing the callback this way
    folly::call_once(once_, &HistogramWrapper::doApplySpecLocked, this);
  }

  folly::once_flag once_;
  std::string key_;
  internal::HistogramSpec spec_;
};

class MinuteOnlyHistogram : public HistogramWrapper {
 public:
  template <typename... Args>
  MinuteOnlyHistogram(
      const std::string& key,
      int64_t bucketWidth,
      int64_t min,
      int64_t max,
      const Args&... args)
      : HistogramWrapper(
            key,
            bucketWidth,
            min,
            max,
            args...,
            MinuteOnlyTimeSeries<CounterType>()) {}
};

/**
 * DynamicTimeseriesWrapper is similar to TimeseriesWrapper, but instead of
 * having a fixed key, it takes a key format (e.g. "foo.{}") containing one or
 * more special placeholders "{}" that can be replaced with a subkey.
 *
 * Sample usage:
 *   DEFINE_dynamic_timeseries(foo, "foo.{}", SUM);
 *   DEFINE_dynamic_timeseries(bar, "bar.{}.{}", SUM);
 *
 *   int main(int argc, char** argv) {
 *     STATS_foo.add(1, "red");
 *     STATS_foo.add(2, 42);
 *     // This will export "foo.red.sum.*" and "foo.42.sum.*".
 *
 *     STATS_bar.add(1, "red", "cat");
 *     STATS_bar.add(2, "blue", 42);
 *     // This will export "bar.red.cat.sum.*" and "bar.blue.42.sum.*".
 *   }
 *
 * Notes: we only support a limited subset of folly::format()'s functionality
 * for better runtime-safety and performance
 * (E.g. we don't support containers as subkeys even though folly::format()
 *  supports them as arguments.)
 */
template <int N> // N is the number of subkeys.
class DynamicTimeseriesWrapper {
 public:
  DynamicTimeseriesWrapper(
      std::string keyFormat,
      std::vector<ExportType> exportTypes)
      : key_(
            std::move(keyFormat),
            [this](const std::string& key) { prepareKey(key); }),
        exportTypes_(std::move(exportTypes)) {}

  // This overload is called from the DEFINE_dynamic_timeseries macro when the
  // first argument is a string and the remaining arguments are all ExportTypes
  // such as SUM or COUNT.
  template <
      typename... Args,
      typename std::enable_if_t<
          folly::Conjunction<
              typename std::is_convertible<Args, ExportType>::type...>::value,
          bool> = true>
  DynamicTimeseriesWrapper(std::string keyFormat, Args... exportTypes)
      : DynamicTimeseriesWrapper(keyFormat, {exportTypes...}) {}

  // This overload is called from the DEFINE_dynamic_timeseries macro when the
  // second argument is an ExportedStat prototype. This allows passing, e.g.
  // fb303::MinuteOnlyTimeseries<int64_t>() into the macro to only export a
  // single timeseries that's collected every minute.
  template <typename... Args>
  DynamicTimeseriesWrapper(
      std::string keyFormat,
      ExportedStat prototype,
      Args... exportTypes)
      : key_(
            std::move(keyFormat),
            [this, prototype = std::move(prototype)](const std::string& key) {
              prepareKey(key, &prototype);
            }),
        exportTypes_({exportTypes...}) {}

  DynamicTimeseriesWrapper(DynamicTimeseriesWrapper&&) = delete;
  DynamicTimeseriesWrapper(const DynamicTimeseriesWrapper&) = delete;

  // "subkeys" must be a list of exactly N strings or integers, one for each
  // subkey.
  // E.g. add(1, "red", "cat");
  //      add(2, "red", 42);
  template <typename... Args>
  void add(int64_t value, Args&&... subkeys) {
    auto const& key = key_.getFormattedKey(std::forward<Args>(subkeys)...);
    tcData().addStatValue(key, value);
  }

  // "subkeys" must be a list of exactly N strings or integers, one for each
  // subkey.
  // E.g. clear("red", "cat");
  //      clear("red", 42);
  template <typename... Args>
  void clear(Args&&... subkeys) {
    auto const& key = key_.getFormattedKey(subkeys...);
    for (const auto exportType : exportTypes_) {
      ThreadCachedServiceData::get()->clearStat(key, exportType);
    }
    tcData().clearTimeseriesSafe(key);
    key_.eraseFormattedKey(std::forward<Args>(subkeys)...);
  }

  // "subkeys" must be a list of exactly N strings or integers, one for each
  // subkey.
  // E.g. addAggregated(36, 12, "red", "cat");
  //      addAggregated(48, 12, "red", 42);
  template <typename... Args>
  void addAggregated(int64_t sum, int64_t numSamples, Args&&... subkeys) {
    auto const& key = key_.getFormattedKey(std::forward<Args>(subkeys)...);
    tcData().addStatValueAggregated(key, sum, numSamples);
  }

  // Exports a specific key without modifying the statistic. This ensures the
  // key exists, which can be useful for avoiding "dead detectors".
  // "subkeys" must be a list of exactly N strings or integers, one for each
  // subkey.
  // E.g. exportKey(1, "red", "cat");
  //      exportKey(2, "red", 42);
  template <typename... Args>
  void exportKey(Args&&... subkeys) {
    // getFormattedKey has a side-effect of preparing the key, so we just need
    // to call it to ensure the stat is exported.
    (void)key_.getFormattedKey(std::forward<Args>(subkeys)...);
  }

  // Returns a copy of the global map.
  // Only for debugging; not designed to be efficient.
  typename internal::FormattedKeyHolder<N>::GlobalMap getMap() const {
    return key_.getMap();
  }

  template <typename... Args>
  const std::string& prepareFormattedKey(Args&&... subkeys) {
    // getFormattedKey has a side-effect of preparing the key, so we just need
    // to call it to ensure the stat is exported.
    return key_.getFormattedKey(std::forward<Args>(subkeys)...);
  }

  std::shared_ptr<ThreadCachedServiceData::TLTimeseries> getDynamicCounter(
      const std::string& key) {
    return tcData().getTimeseriesSafe(key);
  }

 private:
  inline ThreadCachedServiceData::ThreadLocalStatsMap& tcData() {
    return *ThreadCachedServiceData::getStatsThreadLocal();
  }

  void prepareKey(
      const std::string& key,
      const ExportedStat* prototype = nullptr) {
    for (const auto exportType : exportTypes_) {
      ServiceData::get()->addStatExportType(key, exportType, prototype);
    }
  }

  internal::FormattedKeyHolder<N> key_;
  std::vector<ExportType> exportTypes_;
};

template <int N>
class DynamicHistogramWrapper {
 public:
  template <typename... Args>
  DynamicHistogramWrapper(
      std::string keyFormat,
      int64_t bucketWidth,
      int64_t min,
      int64_t max,
      const Args&... args)
      : key_(
            std::move(keyFormat),
            [this](const std::string& key) { prepareKey(key); }),
        spec_(bucketWidth, min, max, args...) {}
  DynamicHistogramWrapper(const DynamicHistogramWrapper&) = delete;
  DynamicHistogramWrapper(DynamicHistogramWrapper&&) = delete;

  template <typename... Args>
  void add(int64_t value, Args&&... subkeys) {
    (*ThreadCachedServiceData::getStatsThreadLocal())
        .addHistogramValue(
            key_.getFormattedKey(std::forward<Args>(subkeys)...), value);
  }

  // Exports a specific key without modifying the statistic. This ensures the
  // key exists, which can be useful for avoiding "dead detectors".
  // "subkeys" must be a list of exactly N strings or integers, one for each
  // subkey.
  // E.g. exportKey(1, "red", "cat");
  //      exportKey(2, "red", 42);
  template <typename... Args>
  void exportKey(Args&&... subkeys) {
    // getFormattedKey has a side-effect of preparing the key, so we just need
    // to call it to ensure the stat is exported.
    (void)key_.getFormattedKey(std::forward<Args>(subkeys)...);
  }

 private:
  void prepareKey(const std::string& key) {
    spec_.apply(key, fbData.ptr());
  }

  internal::FormattedKeyHolder<N> key_;
  const internal::HistogramSpec spec_;
};

} // namespace fb303
} // namespace facebook

namespace facebook {

/**
 * convenience function; returns the ThreadCachedServiceData singleton
 * which wraps ServiceData::get().
 */
inline fb303::ThreadCachedServiceData& tcData() {
  return *(fb303::ThreadCachedServiceData::get());
}

} // namespace facebook

#define DECLARE_counter(varname) \
  extern ::facebook::fb303::CounterWrapper STATS_##varname

#define DECLARE_timeseries(varname) \
  extern ::facebook::fb303::TimeseriesWrapper STATS_##varname

#define DECLARE_histogram(varname) \
  extern ::facebook::fb303::HistogramWrapper STATS_##varname

#define DECLARE_dynamic_timeseries(varname, keyNumArgs) \
  extern ::facebook::fb303::DynamicTimeseriesWrapper<keyNumArgs> STATS_##varname

#define DECLARE_dynamic_histogram(varname, keyNumArgs) \
  extern ::facebook::fb303::DynamicHistogramWrapper<keyNumArgs> STATS_##varname

#define DEFINE_counter(varname, ...) \
  ::facebook::fb303::CounterWrapper STATS_##varname(#varname, ##__VA_ARGS__)

#define DEFINE_timeseries(varname, ...) \
  ::facebook::fb303::TimeseriesWrapper STATS_##varname(#varname, ##__VA_ARGS__)

#define DEFINE_histogram(varname, ...) \
  ::facebook::fb303::HistogramWrapper STATS_##varname(#varname, ##__VA_ARGS__)

// We use this function to extract the number of placeholders from our keyformat
// at compile-time.
// This also ensures that our keyformat is a constexpr.
constexpr int countPlaceholders(folly::StringPiece keyformat) {
  return keyformat.size() < 2
      ? 0
      : ((*keyformat.begin() == '{' && *(keyformat.begin() + 1) == '}')
             ? (1 +
                countPlaceholders(
                    folly::range(keyformat.begin() + 2, keyformat.end())))
             : countPlaceholders(
                   folly::range(keyformat.begin() + 1, keyformat.end())));
}

#define DEFINE_dynamic_timeseries(varname, keyformat, ...)                  \
  static_assert(                                                            \
      countPlaceholders(keyformat) > 0,                                     \
      "Must have at least one placeholder.");                               \
  ::facebook::fb303::DynamicTimeseriesWrapper<countPlaceholders(keyformat)> \
      STATS_##varname(keyformat, ##__VA_ARGS__)

#define DEFINE_dynamic_histogram(                                          \
    varname, keyformat, bucketWidth, min, max, ...)                        \
  static_assert(                                                           \
      countPlaceholders(keyformat) > 0,                                    \
      "Must have at least one placeholder.");                              \
  ::facebook::fb303::DynamicHistogramWrapper<countPlaceholders(keyformat)> \
      STATS_##varname(keyformat, bucketWidth, min, max, __VA_ARGS__)

#define DECLARE_DYNAMIC_INSTANCE(timeseries, var, ...)     \
  const std::string key##var =                             \
      STATS_##timeseries.prepareFormattedKey(__VA_ARGS__); \
  auto& var = *STATS_##timeseries.getDynamicCounter(key##var);
