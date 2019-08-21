/*
 * Copyright 2004-present Facebook, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#pragma once

#include <fb303/ThreadLocalStats.h>

#include <fb303/LegacyClock.h>
#include <fb303/TimeseriesExporter.h>
#include <folly/Conv.h>

#include <glog/logging.h>

namespace facebook {
namespace fb303 {

/*
 * TLStatT
 */

template <class LockTraits>
TLStatT<LockTraits>::TLStatT(Container* stats, folly::StringPiece name)
    : containerAndLock_{stats}, name_(name.str()) {}

template <class LockTraits>
TLStatT<LockTraits>::~TLStatT() {
  // We should never be registered with a container at this point.
  // Our subclass should always call preDestroy() in their destructor, before
  // our destructor is invoked.
  //
  // Normally getContainer() should return null here.  However, in some rare
  // cases getContainer() may still have a container set, but we still should
  // not be registered.  In particular, this can happen when the subclass
  // constructor throws.  containerAndLock_ will have been set by our
  // constructor, but we should not be registered with the container yet, since
  // that only happens as the very last step of the subclass constructor, in
  // postInit()/finishMove().
  DCHECK(getContainer() == nullptr || !getContainer()->isRegistered(this));
}

template <class LockTraits>
void TLStatT<LockTraits>::postInit(Container* stats) {
  // Register ourself with our ThreadLocalStats container.
  //
  // This is done in postInit(), as this should be the very last step of
  // construction.  As soon as we call registerStat(), other threads may start
  // calling aggregate() on us.  This should only happen once we are fully
  // constructed.
  stats->registerStat(this);
}

template <class LockTraits>
void TLStatT<LockTraits>::preDestroy() {
  // Call clearContainer().  It does all of the cleanup that we need:
  // it aggregates the stats one final time, and unregisters ourself from the
  // container.
  //
  // This is done in preDestroy() as we need to unregister ourself before we
  // start cleaning up.  Calling unregisterStat() prevents other threads from
  // calling aggregate() on us again.  We have to stop all calls to aggregate()
  // before we start destroying our state.
  clearContainer();
}

/*
 * Constructor for subclasses to invoke when performing move construction.
 *
 * This calls other.clearContainer(), and initializes our container and name
 * from the other stat's data.
 *
 * After invoking this constructor the subclass should move construct its
 * statistic data, and then call finishMove() to complete registration of this
 * TLStat with the container.
 */
template <class LockTraits>
TLStatT<LockTraits>::TLStatT(SubclassMove, TLStatT<LockTraits>& other) noexcept(
    false)
    : // Call other.clearContainer() on the other stat, to aggregate its data
      // and unregister it from its container.  Initialize containerAndLock_
      // with the result, but note that we are not registered with the
      // container yet.
      containerAndLock_{other.clearContainer()},
      // Move other.name_ to our name_.  Note that it is important that this
      // step happens only after calling other.clearContainer().
      name_{std::move(other.name_)} {}

template <class LockTraits>
void TLStatT<LockTraits>::finishMove() {
  // Register ourself with the container stored in containerAndLock_
  auto container = getContainer();
  if (container) {
    container->registerStat(this);
  }
}

template <typename LockTraits>
template <typename Fn>
void TLStatT<LockTraits>::moveAssignment(
    TLStatT<LockTraits>& other,
    Fn&& moveContents) {
  // Self-move is always a no-op.
  if (&other == this) {
    return;
  }

  // Remove us from our current container.
  // This aggregates our stats, and makes sure that aggregate() won't be called
  // on us during the move operation.
  clearContainer();

  // Remove the other TLStatT from its container.
  //
  // We do not set containerAndLock_ yet.  We only set that below, after
  // moveContents() and container->registerStat() have both succeeded.
  // This ensures that containerAndLock_ is never set when we are not
  // registered.
  auto container = other.clearContainer();

  // Take the other stat's name.
  name_ = std::move(other.name_);

  // Call moveContents() to move the stat data structures
  moveContents();

  // Register ourself with the container, and set containerAndLock_
  if (container) {
    // Call registerStat() before updating containerAndLock_,
    // so that containerAndLock_ will still be null if registerStat() throws.
    container->registerStat(this);

    StatGuard guard(this);
    LockTraits::initContainer(guard, &containerAndLock_, container);
  }
}

template <class LockTraits>
ThreadLocalStatsT<LockTraits>* TLStatT<LockTraits>::clearContainer() {
  auto* container = getContainer();
  if (!container) {
    return nullptr;
  }

  // Aggregate the statistics one last time to ensure that we don't lose data
  // cached since the last aggregation.
  aggregate(std::chrono::seconds(get_legacy_stats_time()));

  {
    StatGuard guard(this);
    LockTraits::clearContainer(guard, &containerAndLock_);
  }

  // Make sure we aren't holding our stat lock when we call unregisterStat().
  // unregisterStat() will acquire the ThreadLocalStats's main lock, and we
  // should never be holding one of the individual stat locks when we try to
  // acquire the main lock.
  container->unregisterStat(this);
  return container;
}

template <class LockTraits>
ThreadLocalStatsT<LockTraits>* TLStatT<LockTraits>::checkContainer(
    const char* errorMsg) {
  auto* container = getContainer();
  if (!container) {
    throw std::runtime_error(folly::to<std::string>(
        name_,
        ": ThreadLocalStats container has already been "
        "destroyed while ",
        errorMsg));
  }
  return container;
}

/*
 * TLTimeseriesT
 */

template <class LockTraits>
TLTimeseriesT<LockTraits>::TLTimeseriesT(
    ThreadLocalStatsT<LockTraits>* stats,
    folly::StringPiece name)
    : TLStatT<LockTraits>(stats, name) {
  init(stats);
}

template <class LockTraits>
TLTimeseriesT<LockTraits>::TLTimeseriesT(TLTimeseriesT&& other) noexcept(false)
    : TLStatT<LockTraits>{TLStatT<LockTraits>::SUBCLASS_MOVE, other},
      // Move construct globalStat_.
      //
      // We don't need to hold StatGuard while doing this.  StatGuard is only
      // used to protect count_ and sum_.  The caller is responsible for
      // providing their own synchronization around operations that change our
      // registration state.
      globalStat_{std::move(other.globalStat_)} {
  // We don't need to update count_ and sum_ here.
  // other.count_ and other.sum_ should always be 0 since the TLStatT
  // SUBCLASS_MOVE constructor just called aggregate() on the other stat.

  // Call finishMove()
  this->finishMove();
}

template <class LockTraits>
TLTimeseriesT<LockTraits>& TLTimeseriesT<LockTraits>::operator=(
    TLTimeseriesT&& other) noexcept(false) {
  this->moveAssignment(other, [&] {
    globalStat_.swap(other.globalStat_);
    // We don't need to move sum_ or count_: moveAssignment() performs
    // aggregation before calling us, so they should be 0 in both ourself
    // and the other TLTimeseries now.
  });
  return *this;
}

template <class LockTraits>
TLTimeseriesT<LockTraits>::~TLTimeseriesT() {
  this->preDestroy();
}

template <class LockTraits>
void TLTimeseriesT<LockTraits>::exportStat(fb303::ExportType exportType) {
  // Note: each thread has a TLTimeseries instance for this statistic,
  // so each thread will call exportStat() independently.
  //
  // This shouldn't cause problems: exportStat() holds a lock, and later
  // calls to re-export the stat with the same stat type are essentially
  // no-ops.  Therefore we don't worry about registering the stat in exactly
  // one thread for now.
  auto statMap = this->checkContainer("exporting a stat")->getStatsMap();
  statMap->exportStat(globalStat_, this->name(), exportType);
}

template <class LockTraits>
void TLTimeseriesT<LockTraits>::aggregate(std::chrono::seconds now) {
  int64_t currentSum;
  int64_t currentCount;
  {
    StatGuard g(this);
    currentSum = sum_;
    currentCount = count_;
    sum_ = 0;
    count_ = 0;
  }

  if (currentCount == 0) {
    return;
  }

  // Note that we record all of the data points since the last call to
  // aggregate() in the same second.  If aggregate is called once a second this
  // is no problem.  If it is called less than once a second, some values might
  // end up in the wrong bucket, making the buckets slightly uneven.
  globalStat_.addValueAggregated(now, currentSum, currentCount);
}

template <class LockTraits>
void TLTimeseriesT<LockTraits>::init(ThreadLocalStatsT<LockTraits>* stats) {
  globalStat_ = stats->getStatsMap()->getLockableStatNoExport(this->name());
  this->postInit(stats);
}

template <class LockTraits>
void TLTimeseriesT<LockTraits>::init(
    size_t numBuckets,
    size_t numLevels,
    const int levelDurations[],
    ThreadLocalStatsT<LockTraits>* stats) {
  ExportedStat levels(numLevels, numBuckets, levelDurations);
  globalStat_ = stats->getStatsMap()->getLockableStatNoExport(
      this->name(), nullptr, &levels);
  this->postInit(stats);
}

/*
 * TLHistogramT
 */
template <class LockTraits>
TLHistogramT<LockTraits>::TLHistogramT(
    ThreadLocalStatsT<LockTraits>* stats,
    folly::StringPiece name,
    int64_t bucketWidth,
    int64_t min,
    int64_t max)
    : TLStatT<LockTraits>(stats, name),
      globalStat_(),
      simpleHistogram_(bucketWidth, min, max) {
  initGlobalStat(stats);
  this->postInit(stats);
}

template <class LockTraits>
TLHistogramT<LockTraits>::TLHistogramT(
    ThreadLocalStatsT<LockTraits>* stats,
    folly::StringPiece name,
    const ExportedHistogramMapImpl::LockableHistogram& globalStat)
    : TLStatT<LockTraits>(stats, name),
      globalStat_(globalStat),
      // The bucket size, minimum, and maximum are fixed, and read-only once
      // a TimeseriesHistogram is created.  Therefore, it is safe to access them
      // here without holding the lock.
      //
      // (Just in case the TimeseriesHistogram behavior is ever changed in the
      // future, I would feel more comfortable holding the globalStat.first lock
      // here while accessing globalStat.second.  However, this is difficult to
      // do in the middle of the initializer list, and simpleHistogram_ requires
      // these constructor arguments.)
      simpleHistogram_(
          globalStat.getBucketSize(),
          globalStat.getMin(),
          globalStat.getMax()) {
  this->postInit(stats);
}

template <class LockTraits>
TLHistogramT<LockTraits>::TLHistogramT(TLHistogramT&& other) noexcept(false)
    : TLStatT<LockTraits>{TLStatT<LockTraits>::SUBCLASS_MOVE, other},
      globalStat_{std::move(other.globalStat_)},
      simpleHistogram_{other.simpleHistogram_.getBucketSize(),
                       other.simpleHistogram_.getMin(),
                       other.simpleHistogram_.getMax()} {
  // We don't need to copy the simpleHistogram_ data:
  // The SUBCLASS_MOVE constructor just called other.aggregate(), so
  // other.simpleHistogram_ should be empty now.

  // Call finishMove()
  this->finishMove();
}

template <class LockTraits>
TLHistogramT<LockTraits>& TLHistogramT<LockTraits>::operator=(
    TLHistogramT&& other) noexcept(false) {
  this->moveAssignment(other, [&] {
    // Move globalStat_.
    globalStat_.swap(other.globalStat_);

    // Update simpleHistogram_ to have the desired parameters.
    // It should already be empty since the clearContainer() call above will
    // have just aggregated it.
    //
    // We hold the StatGuards during this operation just to be safe.  However,
    // the caller should perform their own external synchronization though, and
    // should ensure that no other threads are currently updating the data,
    // since this operation updates our container registration status.
    fb303::CounterType bucketSize;
    fb303::CounterType min;
    fb303::CounterType max;
    {
      StatGuard g(&other);
      bucketSize = other.simpleHistogram_.getBucketSize();
      min = other.simpleHistogram_.getMin();
      max = other.simpleHistogram_.getMax();
    }
    {
      StatGuard g(this);
      DCHECK_EQ(0, simpleHistogram_.computeTotalCount());
      simpleHistogram_ =
          folly::Histogram<fb303::CounterType>{bucketSize, min, max};
    }
  });

  return *this;
}

template <class LockTraits>
TLHistogramT<LockTraits>::~TLHistogramT() {
  this->preDestroy();
}

template <class LockTraits>
int64_t TLHistogramT<LockTraits>::getBucketSize() const {
  StatGuard g(this);
  return simpleHistogram_.getBucketSize();
}

template <class LockTraits>
int64_t TLHistogramT<LockTraits>::getMin() const {
  StatGuard g(this);
  return simpleHistogram_.getMin();
}

template <class LockTraits>
int64_t TLHistogramT<LockTraits>::getMax() const {
  StatGuard g(this);
  return simpleHistogram_.getMax();
}

template <class LockTraits>
void TLHistogramT<LockTraits>::aggregate(std::chrono::seconds now) {
  StatGuard g(this);
  if (!dirty_) {
    return;
  }
  globalStat_.addValues(now, simpleHistogram_);
  simpleHistogram_.clear();
  dirty_ = false;
}

template <class LockTraits>
void TLHistogramT<LockTraits>::initGlobalStat(
    ThreadLocalStatsT<LockTraits>* stats) {
  fb303::ExportedHistogram histToCopy(
      simpleHistogram_.getBucketSize(),
      simpleHistogram_.getMin(),
      simpleHistogram_.getMax());
  globalStat_ = stats->getHistogramMap()->getOrCreateLockableHistogram(
      this->name(), &histToCopy);
}

/*
 * TLCounterT
 */

template <class LockTraits>
TLCounterT<LockTraits>::~TLCounterT() {
  this->preDestroy();
}

template <class LockTraits>
TLCounterT<LockTraits>::TLCounterT(TLCounterT&& other) noexcept(false)
    : TLStatT<LockTraits>(TLStatT<LockTraits>::SUBCLASS_MOVE, other) {
  // We don't need to update value_ here.  other.value_ should already be 0
  // since we just finished aggregating it in startMove().

  // Call finishMove()
  this->finishMove();
}

template <class LockTraits>
TLCounterT<LockTraits>& TLCounterT<LockTraits>::operator=(
    TLCounterT&& other) noexcept(false) {
  this->moveAssignment(other, [&] {
    // We don't need to update value_ here.  Both value_ and other.value_
    // should have been reset to 0 by aggregating them in startMove().
  });
  return *this;
}

template <class LockTraits>
void TLCounterT<LockTraits>::aggregate(std::chrono::seconds /*now*/) {
  aggregate();
}

template <class LockTraits>
void TLCounterT<LockTraits>::aggregate() {
  auto* container = this->getContainer();
  if (!container) {
    // Do nothing if the container is currently unset.
    //
    // This can happen in TLStatsThreadSafe mode when the TLCounter is
    // currently in the process of being unregistered or destroyed.
    // - The ThreadLocalStatsT::aggregate() function may see that this object
    //   is still registered when it runs, causing it to call aggregate().
    // - However, a clearContainer() call may be occurring simultaneously in
    //   another thread.  This will clear containerAndLock_ before
    //   unregistering us from the container.  (The unregistration will block
    //   until ThreadLocalStatsT::aggregate() completes, since it has to wait
    //   on the ThreadLocalStats MainLock.)
    return;
  }

  auto delta = value_.reset();
  if (delta == 0) {
    return;
  }

  auto serviceData = container->getServiceData();
  serviceData->incrementCounter(this->name(), delta);
}

/*
 * ThreadLocalStatsT methods
 */

template <class LockTraits>
ThreadLocalStatsT<LockTraits>::ThreadLocalStatsT(ServiceData* serviceData)
    : serviceData_(serviceData ? serviceData : fbData.ptr()) {}

template <class LockTraits>
ThreadLocalStatsT<LockTraits>::~ThreadLocalStatsT() {
  if (!tlStats_.empty()) {
    LOG(WARNING) << "Deleting parent container while " << tlStats_.size()
                 << " stats are registered:";
    for (auto stat = tlStats_.begin(); stat != tlStats_.end();) {
      VLOG(1) << " - " << (*stat)->name();
      auto toClear = stat++;
      (*toClear)->clearContainer();
    }
  }

  DCHECK(tlStats_.empty());
}

template <class LockTraits>
void ThreadLocalStatsT<LockTraits>::registerStat(TLStatT<LockTraits>* stat) {
  RegistryGuard g(lock_);

  auto inserted = tlStats_.insert(stat).second;
  CHECK(inserted) << "attempted to register a stat twice: " << stat->name()
                  << " (" << tlStats_.size() << " registered)";
}

template <class LockTraits>
void ThreadLocalStatsT<LockTraits>::unregisterStat(TLStatT<LockTraits>* stat) {
  RegistryGuard g(lock_);

  size_t numErased = tlStats_.erase(stat);
  (void)numErased; // opt build unused
  DCHECK_EQ(1, numErased)
      << "attempted to unregister a stat that was not registered: "
      << stat->name() << " (" << tlStats_.size() << " registered)";
}

template <class LockTraits>
bool ThreadLocalStatsT<LockTraits>::isRegistered(TLStatT<LockTraits>* stat) {
  RegistryGuard g(lock_);
  auto it = tlStats_.find(stat);
  return (it != tlStats_.end());
}

template <class LockTraits>
void ThreadLocalStatsT<LockTraits>::aggregate() {
  RegistryGuard g(lock_);

  // TODO: In the future it would be nice if the stats code used a
  // std::chrono::time_point instead of just a std::chrono::duration
  std::chrono::seconds now(get_legacy_stats_time());
  for (TLStatT<LockTraits>* stat : tlStats_) {
    stat->aggregate(now);
  }
}

} // namespace fb303
} // namespace facebook
