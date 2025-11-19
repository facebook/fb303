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

#include <fb303/LegacyClock.h>
#include <fb303/TLStatsLockTraits.h>
#include <fb303/TimeseriesExporter.h>
#include <folly/Conv.h>

#include <glog/logging.h>

namespace facebook {
namespace fb303 {

/*
 * TLStatT
 */

template <class LockTraits>
TLStatT<LockTraits>::TLStatT(const Container* stats, folly::StringPiece name)
    : link_{typename detail::TLStatLinkPtr<LockTraits>::FromOther{}, stats->link_},
      name_(TLStatNameSet::get(name)) {}

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
  DCHECK(!link_.isLinked());
}

template <class LockTraits>
void TLStatT<LockTraits>::postInit() {
  // Register ourself with our ThreadLocalStats container.
  //
  // This is done in postInit(), as this should be the very last step of
  // construction.  As soon as we call registerStat(), other threads may start
  // calling aggregate() on us.  This should only happen once we are fully
  // constructed.
  link();
}

template <class LockTraits>
void TLStatT<LockTraits>::preDestroy() {
  // Unregister ourself from our container. This is done in preDestroy() to
  // prevent another thread from aggregating us while we are cleaning up.
  unlink();
}

template <class LockTraits>
void TLStatT<LockTraits>::link() {
  if (link_.linked_) {
    return;
  }

  std::unique_lock<typename LockTraits::RegistryLock> guard(
      link_->mutex_, std::try_to_lock);

  if (!guard.owns_lock()) {
    // Failed to acquire the lock, add to pending list
    if (link_->container_) {
      link_->container_->linkPending_.wlock()->push_back(this);
    }
    link_.linked_ = true;
    return;
  }

  // Successfully acquired the lock, insert into the container
  if (link_->container_) {
    bool inserted = link_->container_->tlStats_.insert(this)
                        .second; // May throw, so do this first.
    CHECK(inserted) << "attempted to register a stat twice: " << name() << "("
                    << link_->container_->tlStats_.size() << " registered)";
    if (link_->container_->tlStats_.size() == 1) {
      link_->container_->tlStatsEmpty_ = false;
    }
  }
  link_.linked_ = true;
}

template <class LockTraits>
void TLStatT<LockTraits>::unlink() {
  if (!link_.linked_) {
    return;
  }

  // Before we unlink from the container, aggegrate one final time.
  aggregate(std::chrono::seconds{get_legacy_stats_time()});

  // Acquire the registry lock. This prevents ThreadLocalStats from trying to
  // call aggregate() on this TLStat while we update the link_ pointer.
  auto guard = link_->lock();
  if (link_->container_) {
    size_t erased = link_->container_->tlStats_.erase(this); // noexcept
    CHECK(erased) << "attempted to unregister a stat that was not registered: "
                  << name() << " (" << link_->container_->tlStats_.size()
                  << " registered)";
    if (link_->container_->tlStats_.size() == 0) {
      link_->container_->tlStatsEmpty_ = true;
    }
  }
  link_.linked_ = false;
}

/*
 * Constructor for subclasses to invoke when performing move construction.
 *
 * This unlinks the other stat from its container and initializes our
 * name from the other stat's data.
 *
 * After invoking this constructor the subclass should move construct its
 * statistic data, and then call finishMove() to complete registration of this
 * TLStat with the container.
 */
template <class LockTraits>
TLStatT<LockTraits>::TLStatT(
    typename TLStatT<LockTraits>::SubclassMoveTag,
    TLStatT<LockTraits>& other) noexcept(false)
    // Copy a reference to the TLStatLink, but don't link us into the container
    // until finishMove().
    : link_{
          typename detail::TLStatLinkPtr<LockTraits>::FromOther{},
          other.link_} {
  other.unlink();

  // Move other.name_ to our name_.  Note that it is important that this
  // step happens only after unlinking the other from the container.
  name_ = std::move(other.name_);
}

template <class LockTraits>
void TLStatT<LockTraits>::finishMove() {
  // Register ourself with the linked container.
  link();
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

  // Remove us from our container while moving stats around.
  unlink();

  // Remove the other TLStatT from its container.  Wait to relink
  // ourselves until moveContents has succeeded.
  other.unlink();

  // Take the other stat's name and container.
  link_.replaceFromOther(other.link_);
  name_ = std::move(other.name_);

  // Move the stat data structures.
  moveContents();

  // Now it's safe to register ourselves with our new container.
  link();
}

template <typename LockTraits>
template <typename Fn>
auto TLStatT<LockTraits>::withContainerChecked(const char* errorMsg, Fn&& fn) {
  auto guard = link_->lock();
  if (!link_->container_) {
    throw std::runtime_error(
        folly::to<std::string>(
            name(),
            ": ThreadLocalStats container has already been destroyed while ",
            errorMsg));
  }

  return fn(*link_->container_);
}

template <typename LockTraits>
bool TLStatT<LockTraits>::shouldUpdateGlobalStatsOnRead() const {
  return link_->shouldUpdateGlobalStatsOnRead();
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
    : TLStatT<
          LockTraits>{typename TLStatT<LockTraits>::SubclassMoveTag{}, other},
      // Move construct globalStat_.
      //
      // We don't need to hold StatGuard while doing this.  StatGuard is only
      // used to protect count_ and sum_.  The caller is responsible for
      // providing their own synchronization around operations that change our
      // registration state.
      globalStat_{std::move(other.globalStat_)} {
  // We don't need to update count_ and sum_ here.
  // other.count_ and other.sum_ should always be 0 since the TLStatT
  // SubclassMove constructor just called aggregate() on the other stat.

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
  auto statMap = this->withContainerChecked(
      "exporting a stat",
      [](Container& container) { return container.getStatsMap(); });

  // We only update the values when aggregating so if we keep calling update()
  // on the stat when reading it eventually it will decay by 100% / numBuckets
  // at the next second boundary.
  // Instead, we implicitly update it during aggregation itself,
  // and the aggregation is guaranteed to be done periodically,
  // usually once every second.
  statMap->exportStat(
      globalStat_,
      this->name(),
      exportType,
      this->shouldUpdateGlobalStatsOnRead());
}

template <class LockTraits>
void TLTimeseriesT<LockTraits>::aggregate(std::chrono::seconds now) {
  auto [currentCount, currentSum] = value_.reset();
  auto update = !this->shouldUpdateGlobalStatsOnRead();
  if (currentCount == 0 && !update) {
    return;
  }
  auto lockedStatPtr = globalStat_.lock();
  if (currentCount != 0) {
    // Note that we record all of the data points since the last call to
    // aggregate() in the same second.  If aggregate is called once a second
    // this is no problem.  If it is called less than once a second, some values
    // might end up in the wrong bucket, making the buckets slightly uneven.
    lockedStatPtr->addValueAggregated(now, currentSum, currentCount);
  }
  if (update) {
    // We must call update() after aggregation at least once so that subsequent
    // reads see it, and so that the stat decays properly even if no samples
    // were added.
    lockedStatPtr->update(TimePoint(now));
  }
}

template <class LockTraits>
void TLTimeseriesT<LockTraits>::init(ThreadLocalStatsT<LockTraits>* stats) {
  globalStat_ = stats->getStatsMap()->getLockableStatNoExport(this->name());
  this->postInit();
}

template <class LockTraits>
void TLTimeseriesT<LockTraits>::init(
    size_t numBuckets,
    size_t numLevels,
    const ExportedStat::Duration levelDurations[],
    ThreadLocalStatsT<LockTraits>* stats) {
  ExportedStat levels(numLevels, numBuckets, levelDurations);
  globalStat_ = stats->getStatsMap()->getLockableStatNoExport(
      this->name(), nullptr, &levels);
  this->postInit();
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
  this->postInit();
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
  this->postInit();
}

template <class LockTraits>
TLHistogramT<LockTraits>::TLHistogramT(TLHistogramT&& other) noexcept(false)
    : TLStatT<
          LockTraits>{typename TLStatT<LockTraits>::SubclassMoveTag{}, other},
      globalStat_{std::move(other.globalStat_)},
      simpleHistogram_{
          other.simpleHistogram_.getBucketSize(),
          other.simpleHistogram_.getMin(),
          other.simpleHistogram_.getMax()} {
  // We don't need to copy the simpleHistogram_ data:
  // The SubclassMove constructor just called other.aggregate(), so
  // other.simpleHistogram_ should be empty now.

  this->finishMove();
}

template <class LockTraits>
TLHistogramT<LockTraits>& TLHistogramT<LockTraits>::operator=(
    TLHistogramT&& other) noexcept(false) {
  this->moveAssignment(other, [&] {
    // Move globalStat_.
    globalStat_.swap(other.globalStat_);

    // Update simpleHistogram_ to have the desired parameters.
    // It should already be empty since the moveAssignment() call above will
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
      std::unique_lock g{this->statLock_};
      bucketSize = other.simpleHistogram_.getBucketSize();
      min = other.simpleHistogram_.getMin();
      max = other.simpleHistogram_.getMax();
    }
    {
      std::unique_lock g{this->statLock_};
      DCHECK_EQ(0u, simpleHistogram_.computeTotalCount());
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
  std::unique_lock g{this->statLock_};
  return simpleHistogram_.getBucketSize();
}

template <class LockTraits>
int64_t TLHistogramT<LockTraits>::getMin() const {
  std::unique_lock g{this->statLock_};
  return simpleHistogram_.getMin();
}

template <class LockTraits>
int64_t TLHistogramT<LockTraits>::getMax() const {
  std::unique_lock g{this->statLock_};
  return simpleHistogram_.getMax();
}

template <class LockTraits>
void TLHistogramT<LockTraits>::aggregate(std::chrono::seconds now) {
  std::unique_lock g{this->statLock_};
  if (!dirty_) {
    return;
  }
  globalStat_.addValues(ExportedHistogramMap::TimePoint(now), simpleHistogram_);
  simpleHistogram_.clear();
  dirty_ = false;
}

template <class LockTraits>
void TLHistogramT<LockTraits>::initGlobalStat(
    ThreadLocalStatsT<LockTraits>* stats) {
  globalStat_ =
      stats->getHistogramMap()->getOrCreateLockableHistogram(this->name(), [&] {
        return fb303::ExportedHistogram{
            simpleHistogram_.getBucketSize(),
            simpleHistogram_.getMin(),
            simpleHistogram_.getMax()};
      });
}

template <class LockTraits>
ExportedHistogramMapImpl* TLHistogramT<LockTraits>::getHistogramMap(
    const char* errorMsg) {
  return this->withContainerChecked(errorMsg, [](Container& container) {
    return container.getHistogramMap();
  });
}

/*
 * TLCounterT
 */

template <class LockTraits>
TLCounterT<LockTraits>::TLCounterT(
    ThreadLocalStatsT<LockTraits>* stats,
    folly::StringPiece name)
    : TLStatT<LockTraits>(stats, name), serviceData_(stats->getServiceData()) {
  this->postInit();
}

template <class LockTraits>
TLCounterT<LockTraits>::~TLCounterT() {
  this->preDestroy();
}

template <class LockTraits>
TLCounterT<LockTraits>::TLCounterT(TLCounterT&& other) noexcept(false)
    : TLStatT<LockTraits>(
          typename TLStatT<LockTraits>::SubclassMoveTag{},
          other),
      serviceData_(other.serviceData_) {
  this->finishMove();
}

template <class LockTraits>
TLCounterT<LockTraits>& TLCounterT<LockTraits>::operator=(
    TLCounterT&& other) noexcept(false) {
  this->moveAssignment(other, [&] {
    serviceData_ = other.serviceData_;
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
  auto delta = value_.reset();
  if (delta == 0) {
    return;
  }

  serviceData_->incrementCounter(this->name(), delta);
}

/*
 * ThreadLocalStatsT methods
 */

// NOT THREAD SAFE: Must be called with link_->mutex held.
template <class LockTraits>
void ThreadLocalStatsT<LockTraits>::completePendingLink() {
  // Drain pending stats by swapping out the vector
  std::vector<TLStatT<LockTraits>*> pending;
  linkPending_.wlock()->swap(pending);

  for (auto* stat : pending) {
    bool inserted = tlStats_.insert(stat).second;
    CHECK(inserted) << "attempted to register a stat twice from pending list: "
                    << stat->name() << "(" << tlStats_.size() << " registered)";
    if (tlStats_.size() == 1) {
      tlStatsEmpty_ = false;
    }
  }
}

template <class LockTraits>
ThreadLocalStatsT<LockTraits>::ThreadLocalStatsT(
    ServiceData* serviceData,
    bool updateGlobalStatsOnRead)
    : serviceData_{serviceData ? serviceData : fbData.ptr()},
      updateGlobalStatsOnRead_{updateGlobalStatsOnRead},
      link_{new detail::TLStatLink<LockTraits>{this}} {}

template <class LockTraits>
ThreadLocalStatsT<LockTraits>::~ThreadLocalStatsT() {
  // Under the registry lock, break all links between ThreadLocalStats
  // and the TLStats.
  auto guard = link_->lock();
  link_->container_ = nullptr;
  if (!tlStats_.empty()) {
    LOG(WARNING) << "Deleting parent container while " << tlStats_.size()
                 << " stats are registered:";
  }
  for (auto* stat : tlStats_) {
    VLOG(1) << " - " << stat->name();
  }
  tlStats_.clear();
  tlStatsEmpty_ = true;
}

template <class LockTraits>
uint64_t ThreadLocalStatsT<LockTraits>::aggregate() {
  // There are cases of applications with many threads, of which a large subset
  // have initialized the per-thread container but which threads no longer have
  // any per-thread stats linked into the container.
  //
  // In such cases - i.e., when this container is empty - we can skip acquiring
  // the mutex lock and fetching the current time. Each of these operations can
  // be costly in the aggregate, so we skip them when we can.
  //
  // Note that this check is done outside of the lock so it is approximate. The
  // check can miss concurrent calls to link() and unlink(). But it would catch
  // any calls to link() and unlink() which are not concurrent with the current
  // call to aggregate(), ie, which are ordered with it under happen-before. So
  // results can differ as compared with a similar check which is done strictly
  // under the lock.
  if (FOLLY_LIKELY(tlStatsEmpty_)) {
    return 0;
  }

  auto guard = link_->lock();

  // TODO: In the future it would be nice if the stats code used a
  // std::chrono::time_point instead of just a std::chrono::duration
  std::chrono::seconds now(get_legacy_stats_time());
  for (TLStatT<LockTraits>* stat : tlStats_) {
    stat->aggregate(now);
  }

  return tlStats_.size();
}

} // namespace fb303
} // namespace facebook
