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

#include <folly/Range.h>
#include <folly/stats/Histogram.h>

#include <fb303/ExportType.h>
#include <fb303/ExportedHistogramMapImpl.h>
#include <fb303/ExportedStatMapImpl.h>
#include <fb303/ServiceData.h>

#include <chrono>
#include <string>
#include <unordered_set>

namespace facebook {
namespace fb303 {

template <class LockTraits>
class TLStatT;
template <class LockTraits>
class TLCounterT;
template <class LockTraits>
class TLHistogramT;
template <class LockTraits>
class TLTimeseriesT;

namespace detail {
template <typename LockTraits>
class TLStatLink;
template <typename LockTraits>
class TLStatLinkPtr;
} // namespace detail

/*
 * A ThreadLocalStats object stores thread-local copies of a group of
 * statistics.
 *
 * Benefits
 * --------
 *
 * Using ThreadLocalStats is much more efficient than directly using
 * ServiceData::addStatValue() and ServiceData::addHistogramValue().
 * ThreadLocalStats provides efficieny gains in two ways:
 *
 * - Lockless operation.
 *   Because the statistics are thread local, no locks need to be acquired to
 *   increment the statistics.
 *
 *   (For callers who wish to be able to call aggregate() from other threads,
 *   ThreadLocalStatsT must be used in TLStatsThreadSafe mode.  This does add
 *   some internal synchronization, but is still much lower overhead than
 *   ServiceData.  TLStatsThreadSafe synchronizes on fine grained-spinlocks,
 *   and avoid's ServiceData's highly contended global string lookup locks.)
 *
 * - No string lookups.
 *   ServiceData::addStatValue() and ServiceData::addHistogramValue() both
 *   accept the statistic name as a string.  This makes the operation slower,
 *   as a string lookup has to be performed each time you add a new data point.
 *   Making matters worse, a global lock needs to be held on the name map while
 *   the lookup is being performed.  This lock is typically highly contended as
 *   it needs to be acquired on every stat update from every thread.
 *
 * Usage
 * -----
 *
 * The lack of built-in string lookups does make the model of operation
 * somewhat different from using ServiceData.  Rather than passing in the
 * statistic name when you want to increment the statistic, each stat has to be
 * initialized ahead of time, and stored as a variable.  Typically the easiest
 * way to do this is to make a class that contains all of the thread-local
 * statistics you will need.  You can still perform dynamic string lookups if
 * desired when you have stats whose name is not known until runtime.  However,
 * by doing your own thread-local string lookups only when necessary you can
 * avoid the lock contention required for the global name map.
 *
 * Example
 * -------
 *
 * class MyServiceRequestStats : public ThreadLocalStatsT<TLStatsNoLocking> {
 *  public:
 *   MyServiceRequestStats()
 *     : openConnections_(this, "open_conns", SUM, RATE),
 *       numErrors_(this, "num_errors", SUM, PCT, RATE),
 *       latencies_(this, "latency_ms", 100, 0, 5000,
 *                  AVG, 50, 95, 99) {}
 *
 *   void connectionOpened() {
 *     openConnections_.addValue(1);
 *   }
 *   void connectionClosed() {
 *     openConnections_.addValue(-1);
 *   }
 *
 *   void requestComplete(ErrorCode error, std::chrono::milliseconds duration) {
 *     latencies_.addValue(duration.count());
 *
 *     // Add 1 to numErrors_ for every failed request, and 0 for every
 *     // successful request.  This way the PCT statistic will report the
 *     // percentage of requests with errors.
 *     numErrors_.addValue(error == ErrorCode::NO_ERROR ? 0 : 1);
 *   }
 *
 *  private:
 *   TLCounter openConnections_;
 *   TLTimeseries numErrors_;
 *   TLHistogram latencies_;
 * };
 *
 * The code in ti/proxygen/http/HTTPProxyStats.h also contains a more extensive
 * real-world example showing how to use ThreadLocalStats, including using
 * dynamically named stats.
 *
 * Aggregation
 * -----------
 *
 * Each ThreadLocalStats object caches statistics updates in the current
 * thread, and publishes them to the global ServiceData object only when
 * aggregate() is called.
 *
 * aggregate() must be called periodically to maintain up-to-date information
 * in the global ServiceData object.  Ideally this method should be called once
 * a second.
 *
 * See the comments for the aggregate() method for more details.
 *
 * Thread Safety
 * -------------
 *
 * ThreadLocalStatsT accepts a LockTraits template parameter to control its
 * operation.  TLStatsNoLocking can be specified to make ThreadLocalStatsT
 * perform no locking at all, for the highest possible performance.  However,
 * in this mode all operations must be performed from a single thread,
 * including any aggregate() calls.
 *
 * TLStatsThreadSafe can be specified as the LockTraits parameter to make
 * ThreadLocalStatsT synchronize its data access.  This will add a small amount
 * of overhead compared to TLStatsNoLocking, but allows aggregate() to be
 * called from other threads.  This option is easier to use in programs that
 * cannot easily be made to call aggregate() regularly in each thread.
 *
 * Note that it is possible to mix and match these two different modes of
 * operation in a single program.  This can be used when you have different
 * classes of threads: threads that can call aggregate() may use
 * ThreadLocalStatsT<TLStatsNoLocking> instances, and threads that require an
 * external thread to call aggregate can use TLTimeseriesT<TLStatsThreadSafe>.
 */
template <class LockTraits>
class ThreadLocalStatsT {
 public:
  typedef TLCounterT<LockTraits> TLCounter;
  typedef TLHistogramT<LockTraits> TLHistogram;
  typedef TLTimeseriesT<LockTraits> TLTimeseries;

  /**
   * Create a new ThreadLocalStats container. Per default (NULL),
   * serviceData will be initialized to facebook::fb303::fbData
   */
  explicit ThreadLocalStatsT(ServiceData* serviceData = nullptr);

  virtual ~ThreadLocalStatsT();

  /**
   * Get the ServiceData that this ThreadLocalStats container aggregates
   * into.
   */
  ServiceData* getServiceData() const {
    return serviceData_;
  }

  /**
   * Get the ExportedStatMapImpl that this ThreadLocalStats container aggregates
   * into.
   */
  ExportedStatMapImpl* getStatsMap() const {
    return serviceData_->getStatMap();
  }

  /**
   * Get the ExportedHistogramMapImpl that this ThreadLocalStats container
   * aggregates into.
   */
  ExportedHistogramMapImpl* getHistogramMap() const {
    return serviceData_->getHistogramMap();
  }

  /**
   * Aggregate all of the thread local stats into the global stats containers.
   *
   * aggregate() must be called periodically to maintain up-to-date information
   * in the global ServiceData object.  Ideally this method should be called
   * once a second.
   *
   * Note that when using TLStatsNoLocking, aggregate() must be called from the
   * local thread that uses this ThreadLocalStats object.  While aggregate()
   * obtains the proper locks to update the global ServiceData object, no locks
   * are held when accessing the cached data in the local thread.
   *
   * If you wish to be able to call aggregate() from another thread, use
   * ThreadLocalStatsT<TLStatsThreadSafe>.  This adds some performance
   * overhead, as all stat updates now perform synchronization.
   *
   * If you are using asynchronous threads driven by a EventBase main
   * loop, fb303/TLStatsAsyncAggregator.h contains a class that can
   * periodically call aggregate() on a ThreadLocalStats from the
   * EventBase loop.
   */
  void aggregate();

  /**
   * Call this function if you are about to transfer ownership of this
   * ThreadLocalStats object to another thread.
   *
   * This is mainly only used for debug bookkeeping purposes: in debug mode
   * ThreadLocalStats checks to make sure it is always used from the correct
   * thread.  If you are intentionally moving a ThreadLocalStats object to
   * another thread, call swapThreads() to inform the ThreadLocalStats object
   * that it is okay if the next access occurs from a different thread.
   * You are still responsible for performing the correct external
   * synchronization when transferring ownerhsip of this ThreadLocalStats
   * object to the other thread.
   *
   * A common use case for this is if you set up the ThreadLocalStats object in
   * one thread before spawning the thread that will ultimately end up using
   * the ThreadLocalStats object for the lifetime of the program.
   */
  void swapThreads();

 private:
  using TLStat = TLStatT<LockTraits>;

  // Forbidden copy constructor and assignment operator
  ThreadLocalStatsT(const ThreadLocalStatsT&) = delete;
  ThreadLocalStatsT& operator=(const ThreadLocalStatsT&) = delete;

  // The serviceData_ pointer never changes, so does not need locking.
  // ServiceData performs its own synchronization to allow it to be accessed
  // from multiple threads.
  ServiceData* const serviceData_;

  /**
   * ThreadLocalStats and every TLStat in tlStats_ has a pointer to
   * the TLStatLink.  On destruction, ThreadLocalStats clears
   * link_->container_.
   *
   * The registry lock must never be acquired while a stat's lock
   * is held.
   */
  detail::TLStatLinkPtr<LockTraits> link_;

  /**
   * link_->mutex protects access to tlStats_ (when LockTraits actually
   * provides thread-safety guarantees).
   */
  std::unordered_set<TLStat*> tlStats_;

  template <typename T>
  friend class TLStatT;
};

/**
 * Abstract base class for all thread-local stats structures.
 *
 * See TLTimeseries for an example of a concrete subclass.
 */
template <class LockTraits>
class TLStatT {
 public:
  using Container = ThreadLocalStatsT<LockTraits>;

  TLStatT(const Container* stats, folly::StringPiece name);
  virtual ~TLStatT();

  const std::string& name() const {
    return name_;
  }

  virtual void aggregate(std::chrono::seconds now) = 0;

 protected:
  enum SubclassMove { SUBCLASS_MOVE };

  std::unique_lock<typename LockTraits::StatLock> guardStatLock() const {
    // Assert the stat is being used by the thread currently responsible
    // for the container in debug mode.
    LockTraits::willAcquireStatLock(link_->mutex_);
    return std::unique_lock<typename LockTraits::StatLock>{statLock_};
  }

  /**
   * Helper constructor for move-construction of subclasses
   *
   * Callers should call finishMove() as the last step of their move
   * constructor.
   *
   * Subclass move operators unfortunately cannot be noexcept, since
   * registration with the container may fail.  Due to the order of operations,
   * it is unfortunately possible for a move constructor to fail and leave the
   * old stat unregistered as well.  (Failure should be rare, typically only
   * memory allocation failure can cause this to fail.)
   */
  explicit TLStatT(SubclassMove, TLStatT& other) noexcept(false);

  /*
   * Subclasses of TLStat must call postInit() once they have finished
   * constructing their object, as the very last step of construction.
   *
   * This registers the TLStat with its ThreadLocalStats container.  Once this
   * has been completed, the TLStat will be visible to other threads, and they
   * may begin calling aggregate() on it.  This is done as the last step of
   * construction to ensure that the TLStat is not visible to other threads
   * until it is fully destroyed.
   *
   * Similarly, preDestroy() must be called as the first step of destruction.
   */
  void postInit();

  /*
   * Subclasses of TLStat must call preDestroy() as the very first step of
   * destruction.
   *
   * This unregisters the TLStat from the ThreadLocalStats container,
   * preventing other threads from calling aggregate() on this object again.
   * This must be done before the TLStat state begins to be cleaned up.
   */
  void preDestroy();

  /**
   * finishMove() should be called by the subclass as the very last step of
   * move construction.
   */
  void finishMove();

  /**
   * Helper function for subclasses to implement the move assignment operator.
   *
   * This performs the following steps:
   * 1. Returns immediately if this is a self-move.
   * 2. Aggregates any data currently in this TLStat, and unregisters it from
   *    its current container.
   * 3. Aggregates any data currently in the other TLStat, and unregisters it
   *    from its current container.
   * 4. Calls the moveContents() function supplied by the caller, which should
   *    perform any steps necessary to remove the statistic data.
   * 5. Registers this stat with its new container.
   *
   * This is noexcept(false) for the same reasons described for the move
   * constructor.
   */
  template <typename Fn>
  void moveAssignment(TLStatT& other, Fn&& moveContents) noexcept(false);

  /**
   * WARNING: The ThreadLocalStats's RegistryLock is held while `fn` runs.
   */
  template <typename Fn>
  auto withContainerChecked(const char* errorMsg, Fn&& fn);

 private:
  /**
   * Explicitly deleted move andy copy constructors.
   *
   * Subclasses can implement their own move constructors, but they must
   * implement it using our TLStatT(SUBCLASS_MOVE, other) constructor above.
   */
  TLStatT(TLStatT&&) = delete;
  TLStatT(const TLStatT&) = delete;

  /**
   * Explicitly deleted move and copy assignment.
   *
   * Subclasses can provide their own overridden assignment operator, using
   * the moveAssignment() helper function above.
   */
  TLStatT& operator=(TLStatT&&) = delete;
  TLStatT& operator=(const TLStatT&) = delete;

  /**
   * Links this structure into its container.
   *
   * Not thread safe. Will only be called in situations where races
   * are already undefined, such as constructors, destructors, and
   * moves.
   */
  void link();

  /**
   * Aggregates this stat one last time, and detaches it from its
   * container. Safe to call if already unlinked.
   *
   * Not thread safe. Will only be called in situations where races
   * are already undefined, such as constructors, destructors, and
   * moves.
   */
  void unlink();

  detail::TLStatLinkPtr<LockTraits> link_;
  std::string name_;

  /**
   * Synchronizes access to this TLStat's value. This class used to
   * use the bottom bit of the container_ pointer as a spin lock which
   * saves some space, but MicroLock is slightly cheaper in the common
   * uncontended case and doesn't degrade as poorly under contention,
   * such as when one thread is updating the stats and another is
   * aggregating.
   *
   * If the space matters, we can buy a word by storing name_ in a
   * folly::fbstring.
   */
  mutable typename LockTraits::StatLock statLock_;
};

/**
 * A thread-local data structure to update a global MultiLevelTimeSeries
 * statistic.
 */
template <class LockTraits>
class TLTimeseriesT : public TLStatT<LockTraits> {
 public:
  using typename TLStatT<LockTraits>::Container;

  TLTimeseriesT(ThreadLocalStatsT<LockTraits>* stats, folly::StringPiece name);

  template <typename... ExportTypes>
  TLTimeseriesT(
      ThreadLocalStatsT<LockTraits>* stats,
      folly::StringPiece name,
      ExportTypes... types)
      : TLStatT<LockTraits>(stats, name), sum_(0), count_(0) {
    init(stats);
    exportStat(types...);
  }

  TLTimeseriesT(
      ThreadLocalStatsT<LockTraits>* stats,
      folly::StringPiece name,
      size_t numBuckets,
      size_t numLevels,
      const int levelDurations[])
      : TLStatT<LockTraits>(stats, name), sum_(0), count_(0) {
    init(numBuckets, numLevels, levelDurations, stats);
  }

  template <typename... ExportTypes>
  TLTimeseriesT(
      ThreadLocalStatsT<LockTraits>* stats,
      folly::StringPiece name,
      size_t numBuckets,
      size_t numLevels,
      const int levelDurations[],
      ExportTypes... types)
      : TLStatT<LockTraits>(stats, name), sum_(0), count_(0) {
    init(numBuckets, numLevels, levelDurations, stats);
    exportStat(types...);
  }

  ~TLTimeseriesT() override;

  /**
   * Move construction.
   */
  TLTimeseriesT(TLTimeseriesT&& other) noexcept(false);

  /**
   * Move assignment.
   *
   * The caller is responsible for synchronizing accesses around this call.  No
   * other threads should be accessing either the moved-to or moved-from
   * TLTimeseriesT during this operation.
   */
  TLTimeseriesT& operator=(TLTimeseriesT&& other) noexcept(false);

  /**
   * Add a new data point
   */
  void addValue(int64_t value) {
    auto g = this->guardStatLock();

    sum_ += value;
    count_ += 1;
  }

  void addValueAggregated(int64_t value, int64_t nsamples) {
    auto g = this->guardStatLock();

    sum_ += value;
    count_ += nsamples;
  }

  void exportStat(ExportType exportType);

  template <typename... ET>
  void exportStat(ExportType exportType, ET... types) {
    exportStat(exportType);
    exportStat(types...);
  }
  void exportStat() {}

  void aggregate(std::chrono::seconds now) override;

  int64_t count() const {
    auto g = this->guardStatLock();
    return count_;
  }

  int64_t sum() const {
    auto g = this->guardStatLock();
    return sum_;
  }

  /**
   * Return const reference to underlying globalStat. Caller is expected to use
   * this judiciously (for sanity checking) instead of trying to update the stat
   * through it.
   */
  const ExportedStatMapImpl::LockableStat& getGlobalStat() {
    return globalStat_;
  }

 private:
  void init(ThreadLocalStatsT<LockTraits>* stats);

  void init(
      size_t numBuckets,
      size_t numLevels,
      const int levelDurations[],
      ThreadLocalStatsT<LockTraits>* stats);

  ExportedStatMapImpl::LockableStat globalStat_;
  int64_t sum_{0};
  int64_t count_{0};
};

/**
 * A thread-local data structure to update a global TimeseriesHistogram
 * statistic.
 */
template <class LockTraits>
class TLHistogramT : public TLStatT<LockTraits> {
 public:
  TLHistogramT(
      ThreadLocalStatsT<LockTraits>* stats,
      folly::StringPiece name,
      int64_t bucketWidth,
      int64_t min,
      int64_t max);

  template <typename... ExportArgs>
  TLHistogramT(
      ThreadLocalStatsT<LockTraits>* stats,
      folly::StringPiece name,
      int64_t bucketWidth,
      int64_t min,
      int64_t max,
      ExportArgs... exportArgs)
      : TLStatT<LockTraits>(stats, name),
        simpleHistogram_(bucketWidth, min, max) {
    initGlobalStat(stats);
    exportStat(exportArgs...);
    this->postInit();
  }

  /*
   * Create a new TLHistogramT from an existing global histogram.
   *
   * The caller is responsible for ensuring that this histogram is already
   * registered in the global histogram map using the specified name.
   */
  TLHistogramT(
      ThreadLocalStatsT<LockTraits>* stats,
      folly::StringPiece name,
      const ExportedHistogramMapImpl::LockableHistogram& globalStat);

  ~TLHistogramT() override;

  /**
   * Move construction.
   */
  TLHistogramT(TLHistogramT&& other) noexcept(false);

  /**
   * Move assignment.
   *
   * The caller is responsible for synchronizing accesses around this call.  No
   * other threads should be accessing either the moved-to or moved-from
   * TLHistogramT during this operation.
   */
  TLHistogramT& operator=(TLHistogramT&& other) noexcept(false);

  int64_t getBucketSize() const;
  int64_t getMin() const;
  int64_t getMax() const;

  void addValue(int64_t value) {
    auto g = this->guardStatLock();
    simpleHistogram_.addValue(value);
    dirty_ = true;
  }

  void addRepeatedValue(int64_t value, int64_t nsamples) {
    auto g = this->guardStatLock();
    simpleHistogram_.addRepeatedValue(value, nsamples);
    dirty_ = true;
  }

  template <typename... Pct>
  void exportPercentile(int percentile, Pct... morePercentiles) {
    getHistogramMap("exporting a percentile")
        ->exportPercentile(this->name(), percentile, morePercentiles...);
  }

  template <typename... Pct>
  void unexportPercentile(Pct... percentiles) {
    getHistogramMap("unexporting a percentile")
        ->unexportPercentile(this->name(), percentiles...);
  }

  /*
   * exportStat() can accept a mixture of ExportType arguments
   * and integer percentiles.
   */
  template <typename... ExportArgs>
  void exportStat(ExportArgs... exportArgs) {
    getHistogramMap("exporting a stat")
        ->exportStat(this->name(), exportArgs...);
  }

  template <typename... ExportArgs>
  void unexportStat(ExportArgs... exportArgs) {
    getHistogramMap("unexporting a percentiles")
        ->unexportStat(this->name(), exportArgs...);
  }

  void aggregate(std::chrono::seconds now) override;

 private:
  using typename TLStatT<LockTraits>::Container;

  void initGlobalStat(ThreadLocalStatsT<LockTraits>* stats);

  ExportedHistogramMapImpl* getHistogramMap(const char* errorMsg);

  ExportedHistogramMapImpl::LockableHistogram globalStat_;
  folly::Histogram<fb303::CounterType> simpleHistogram_;
  bool dirty_{false};
};

/**
 * A thread-local data structure to update a global counter statistic.
 *
 * Counter statistics are a bit different from timeseries and histogram data:
 * rather than tracking a series of data points, a counter tracks just a
 * single value.
 *
 * TLCounter only provides an incrementValue() API.  When multiple TLCounter
 * objects are aggregated, the increments done to all of them are summed and
 * added to the global value.
 *
 * TLCounter does not support any sort of setValue() API.  If you need
 * setValue() behavior you need to update the global stat directly.  Trying to
 * use thread local state for this would result in unpredictable and likely
 * undesirable behavior, as it is not specified which order setValue()
 * operations would be seen when done in different threads.
 */
template <class LockTraits>
class TLCounterT : public TLStatT<LockTraits> {
 public:
  TLCounterT(ThreadLocalStatsT<LockTraits>* stats, folly::StringPiece name);
  ~TLCounterT() override;

  /**
   * Move construction.
   */
  TLCounterT(TLCounterT&& other) noexcept(false);

  /**
   * Move assignment.
   *
   * The caller is responsible for synchronizing accesses around this call.  No
   * other threads should be accessing either the moved-to or moved-from
   * TLCounterT during this operation.
   */
  TLCounterT& operator=(TLCounterT&& other) noexcept(false);

  /**
   * Increment the counter by a specified value.
   *
   * The value may be negative to decrement the counter.
   */
  void incrementValue(fb303::CounterType amount = 1) {
    value_.increment(amount);
  }

  void aggregate(std::chrono::seconds now) override;
  void aggregate();

 private:
  using ValueType =
      typename LockTraits::template CounterType<fb303::CounterType>;

  /**
   * Save a pointer to ServiceData because the container may be
   * destroyed while aggregate is running and it is not safe to
   * read container_ from aggregate().
   */
  ServiceData* serviceData_;

  /**
   * The current thread-local counter delta.
   *
   * Each call to aggregate() adds this value to the global counter, and
   * resets this thread-local value to 0.
   */
  ValueType value_;
};

namespace detail {
/**
 * ThreadLocalStats knows all of its referenced TLStat objects. Each
 * TLStat knows its ThreadLocalStats container.  In TLStatsThreadSafe
 * locking mode, the container and its elements can be destroyed
 * independently on arbitrary threads.  TLStatsLink implements the
 * locking model for the backpointers, ensuring that the bidirectional
 * references between a ThreadLocalStats and its TLStats are
 * atomically kept in sync. It also guarantees that, if a TLStat wants
 * to access its containing ThreadLocalStats, the ThreadLocalStats
 * will remain alive.
 */
template <typename LockTraits>
class TLStatLink {
 public:
  using Container = ThreadLocalStatsT<LockTraits>;
  using Lock = typename LockTraits::RegistryLock;
  using Guard = std::unique_lock<Lock>;

  explicit TLStatLink(Container* container)
      : container_{container}, refCount_{1} {}

  TLStatLink(const TLStatLink&) = delete;
  TLStatLink(TLStatLink&&) = delete;

  TLStatLink& operator=(const TLStatLink&) = delete;
  TLStatLink& operator=(TLStatLink&&) = delete;

  void incRef() {
    Guard guard{mutex_};
    ++refCount_;
  }

  void decRef() {
    size_t after;
    {
      Guard guard{mutex_};
      DCHECK_GT(refCount_, 0u);
      after = --refCount_;
    }
    if (after == 0) {
      delete this;
    }
  }

  void swapThreads() {
    LockTraits::swapThreads(mutex_);
  }

  Guard lock() {
    return Guard{mutex_};
  }

 private:
  /**
   * Protects refcount_, container_, and container_->tlStats_.
   */
  Lock mutex_;

  // If container_ is non-null, then the pointee Container is guaranteed to be
  // alive. ThreadLocalStats's destructor zeroes this pointer.
  Container* container_{nullptr};

  // TODO: It's slightly inefficient to keep the refcount under a
  // mutex. It is not updated simultaneously with container_ or
  // tlStats_, so it could be replaced with std::atomic<size_t> or
  // size_t depending on LockTraits.
  size_t refCount_{0};

  friend class ThreadLocalStatsT<LockTraits>;
  friend class TLStatT<LockTraits>;
  friend class TLStatLinkPtr<LockTraits>;
};

/**
 * TLStatLinkPtr manages the reference count and memory of its
 * TLStatLink. Each ThreadLocalStats has a strong reference to one
 * TLStatLink.
 *
 * TLStatLinkPtr can be in one of two states:
 *
 * - unlinked: has a reference to a TLStatLink, but the TLStat owning
 *   this pointer is not in the Container's registry.
 *
 * - linked: has a reference to a TLStatLink, and the owning TLStat is
 *   in the Container's registry.
 *
 * This complexity exists to make concurrent destruction of ThreadLocalStats and
 * TLStats safe while also enabling move assignment and move construction.
 */
template <typename LockTraits>
class TLStatLinkPtr {
 public:
  struct FromOther {};

  /**
   * Construct the initial link pointer, owned by the container. This
   * one is always unlinked.
   */
  explicit TLStatLinkPtr(TLStatLink<LockTraits>* ptr) : ptr_{ptr} {
    DCHECK(ptr_);
    DCHECK_EQ(1u, ptr_->refCount_);
    // Starts in unlinked state.
  }

  /**
   * Given an existing link pointer, create a new unlinked one.
   */
  TLStatLinkPtr(FromOther, const TLStatLinkPtr& other) : ptr_{other.ptr_} {
    ptr_->incRef();
  }

  ~TLStatLinkPtr() {
    DCHECK(!linked_)
        << "The owner of this linked pointer must unlink before destroying it";
    ptr_->decRef();
  }

  void replaceFromOther(const TLStatLinkPtr& other) {
    DCHECK(!linked_) << "Must be unlinked before replacing";
    DCHECK_NE(this, &other) << "Cannot replace with self";
    ptr_->decRef();
    ptr_ = other.ptr_;
    ptr_->incRef();
  }

  TLStatLink<LockTraits>* operator->() const {
    return ptr_;
  }

  TLStatLink<LockTraits>* get() const {
    return ptr_;
  }

  bool isLinked() const {
    return linked_;
  }

 private:
  /*
   * No copying or moving - each link is associated with a specific
   * TLStat's lifetime.
   */

  TLStatLinkPtr() = delete;
  TLStatLinkPtr(const TLStatLinkPtr&) = delete;
  TLStatLinkPtr(TLStatLinkPtr&&) = delete;
  TLStatLinkPtr& operator=(const TLStatLinkPtr&) = delete;
  TLStatLinkPtr& operator=(TLStatLinkPtr&&) = delete;

  TLStatLink<LockTraits>* ptr_ = nullptr;

  /**
   * Represents whether this Ptr currently represents a valid link
   * between ThreadLocalStats and a TLStat. Transient during a move
   * operation.
   *
   * TODO: Reusing the bottom bit of ptr_ as the linked state would
   * save a pointer in every TLStat.
   */
  bool linked_ = false;

  friend class TLStatT<LockTraits>;
};
} // namespace detail

} // namespace fb303
} // namespace facebook

#include <fb303/ThreadLocalStats-inl.h>
