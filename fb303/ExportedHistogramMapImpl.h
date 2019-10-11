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

#include <fb303/ExportedHistogramMap.h>

namespace facebook {
namespace fb303 {

class ExportedHistogramMapImpl : public ExportedHistogramMap {
 public:
  /**
   * LockableHistogram is a simple wrapper class for the LockAndHistogram object
   * that abstracts out all of the locking, which previously had to be done by
   * the user. This new wrapper class also allows for addValue methods to be
   * directly called on it which will clean up the current counter call sites.
   */
  class LockableHistogram {
   public:
    using Histogram = folly::Histogram<CounterType>;

    /**
     * Creates a LockableHistogram object from a HistogramPtr object. The
     * HistogramPtr object is a shared_ptr holding a SyncHistogram so it holds
     * a nullptr on default.
     */
    LockableHistogram() {}
    explicit LockableHistogram(HistogramPtr hist) : hist_(std::move(hist)) {}

    /**
     * Locks the histogram that is held by this LockableHistogram object
     * and returns the corresponding LockedHistogramPtr. The Histogram remains
     * locked as long as the LockedHistogramPtr remains in scope.
     */
    LockedHistogramPtr makeLockGuard() const {
      return hist_->lock();
    }

    /** Return true if histogram is null. */
    bool isNull() const {
      return hist_ == nullptr;
    }

    /* Swap the HistogramPtrs held by the LockableHistogram objects. */
    void swap(LockableHistogram& h) noexcept {
      hist_.swap(h.hist_);
    }

    /**
     * Return the bucket size of the associated histogram. The histogram remains
     * locked for the duration of this call.
     */
    CounterType getBucketSize() const {
      return hist_->lock()->getBucketSize();
    }

    /**
     * Return the minimum value of this associated histogram. The histogram
     * remains locked for the duration of this call.
     */
    CounterType getMin() const {
      return hist_->lock()->getMin();
    }

    /**
     * Return the maximum value of the associated histogram. The histogram
     * remains locked for the duration of this call.
     */
    CounterType getMax() const {
      return hist_->lock()->getMax();
    }

    /**
     * Add a value to the histogram. The optional 'times' parameter can be used
     * here to add multiple copies of the value at a time. The histogram remains
     * locked for the duration of this call.
     */
    void addValue(time_t now, const CounterType value, int64_t times = 1)
        const {
      hist_->lock()->addValue(now, value, times);
    }

    /**
     * Add a value to the histogram. The optional 'times' parameter can be used
     * here to add multiple copies of the value at a time.
     *
     * This method assumes that the object has already been locked, and requires
     * the LockedHistogramPtr object as parameter.
     */
    void addValueLocked(
        const LockedHistogramPtr& lockedObj,
        time_t now,
        const CounterType value,
        int64_t times = 1) const {
      DCHECK(!lockedObj.isNull());
      lockedObj->addValue(now, value, times);
    }

    /**
     * Update the histogram to include all of the values of another given
     * histogram. The histogram remains locked for the duration of this call.
     */
    void addValues(std::chrono::seconds now, const Histogram& values) const {
      hist_->lock()->addValues(now, values);
    }
    void addValues(time_t now, const Histogram& values) const {
      hist_->lock()->addValues(now, values);
    }

    /**
     * Update the histogram to include all of the values of another given
     * histogram.
     *
     * This method assumes that the object has already been locked, and requires
     * the LockedHistogramPtr object as parameter.
     */
    void addValuesLocked(
        const LockedHistogramPtr& lockedObj,
        time_t now,
        const Histogram& values) const {
      DCHECK(!lockedObj.isNull());
      lockedObj->addValues(now, values);
    }

    /**
     * Update the histogram with the given time value.
     *
     * This method assumes that the object has already been locked, and requires
     * the LockedHistogramPtr object as parameter.
     */
    void updateLocked(const LockedHistogramPtr& lockedObj, time_t now) const {
      DCHECK(!lockedObj.isNull());
      lockedObj->update(now);
    }

    /**
     * Get the percentile estimate of the histogram.
     *
     * This method assumes that the object has already been locked, and requires
     * the LockedHistogramPtr object as parameter.
     */
    CounterType getPercentileEstimateLocked(
        const LockedHistogramPtr& lockedObj,
        double percentile,
        int level) const {
      DCHECK(!lockedObj.isNull());
      return lockedObj->getPercentileEstimate(percentile, level);
    }

   private:
    HistogramPtr hist_;
  };

  /**
   * Creates an ExportedHistogramMapImpl and hooks it up to the given
   * DynamicCounters object for getCounters(), and the given DynamicStrings
   * object for getExportedValues().  The copyMe object provided will be used
   * as a blueprint for new histograms that are created; this is where you set
   * up your bucket ranges and time levels appropriately for your needs.  There
   * is no default set of bucket ranges, so a 'copyMe' object must be provided.
   */
  ExportedHistogramMapImpl(
      DynamicCounters* counters,
      DynamicStrings* strings,
      const ExportedHistogram& copyMe)
      : ExportedHistogramMap(counters, strings, copyMe) {}

  /**
   * Get the LockableHistogram object for the given histogram. This method is
   * identical to getHistogramUnlocked above but returns the LockAndItem object
   * inside of the LockableHistogram instead of returning it directly.
   */

  LockableHistogram getLockableHistogram(folly::StringPiece name) {
    return LockableHistogram(getHistogramUnlocked(name));
  }

  /**
   * Get the LockableHistogram object for the given histogram. This method is
   * identical to getOrCreateUnlocked above but returns the HistogramPtr object
   * inside of the LockableHistogram instead of returning it directly.
   */
  LockableHistogram getOrCreateLockableHistogram(
      folly::StringPiece name,
      const ExportedHistogram* copyMe,
      bool* createdPtr = nullptr) {
    return LockableHistogram(getOrCreateUnlocked(name, copyMe, createdPtr));
  }

  using ExportedHistogramMap::addValue;
  /**
   * Adds multiple copies of value into the stat specified by 'name' at
   * time 'now.'
   *
   * This method is identical to the addValue method in ExportedHistogramMap but
   * is more efficient because it avoids hashtable lookup per operation.
   *
   * This method is primarily left here for backwards compatibility. Users
   * should prefer calling the addValue method directly on the HistogramPtr
   * object using the -> operator instead of using this method.
   */
  void addValue(
      HistogramPtr histogram,
      time_t now,
      CounterType value,
      int64_t times = 1) {
    histogram->lock()->addValue(now, value, times);
  }

  /*
   * Legacy functions that accept time_t instead of std::chrono::seconds
   */

  void addValue(
      folly::StringPiece name,
      time_t now,
      CounterType value,
      int64_t times = 1) {
    return addValue(name, std::chrono::seconds(now), value, times);
  }

  using ExportedHistogramMap::addValues;
  void addValues(
      folly::StringPiece name,
      time_t now,
      const folly::Histogram<CounterType>& values) {
    return addValues(name, std::chrono::seconds(now), values);
  }
  void addValues(
      folly::StringPiece name,
      time_t now,
      const folly::Histogram<CounterType>& values,
      const ExportedHistogram* hist,
      int percentile) {
    return addValues(name, std::chrono::seconds(now), values, hist, percentile);
  }

  void addValues(
      folly::StringPiece name,
      time_t now,
      const folly::Histogram<CounterType>& values,
      const ExportedHistogram* hist,
      folly::small_vector<int> percentiles) {
    return addValues(
        name, std::chrono::seconds(now), values, hist, std::move(percentiles));
  }

 protected:
  HistogramPtr ensureExists(folly::StringPiece name, bool crashIfMissing);
};

// Lock the histogram and calculate the percentile
CounterType getHistogramPercentile(
    const ExportedHistogramMapImpl::LockableHistogram& hist,
    int level,
    double percentile);

} // namespace fb303
} // namespace facebook
