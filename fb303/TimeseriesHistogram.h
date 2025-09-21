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

#include <fb303/Timeseries.h>
#include <folly/stats/Histogram.h>
#include <folly/stats/TimeseriesHistogram.h>

namespace facebook::fb303 {

const std::chrono::seconds kHistogramMinuteTenMinuteHourDurations[] = {
    std::chrono::seconds(60),
    std::chrono::seconds(600),
    std::chrono::seconds(3600),
    std::chrono::seconds(0)};

template <class T>
class HistogramMinuteTenMinuteHourTimeSeries
    : public MultiLevelTimeSeries<
          T,
          folly::LegacyStatsClock<std::chrono::seconds>> {
 public:
  enum Levels {
    MINUTE,
    TEN_MINUTE,
    HOUR,
    ALLTIME,
    NUM_LEVELS,
  };

  HistogramMinuteTenMinuteHourTimeSeries()
      : MultiLevelTimeSeries<T, folly::LegacyStatsClock<std::chrono::seconds>>(
            NUM_LEVELS,
            60,
            kHistogramMinuteTenMinuteHourDurations) {}
};

const std::chrono::seconds kHistogramSubminuteMinuteOnlyDurations[] = {
    std::chrono::seconds(5),
    std::chrono::seconds(10),
    std::chrono::seconds(20),
    std::chrono::seconds(30),
    std::chrono::seconds(60)};

template <class T>
class HistogramSubminuteMinuteOnlyTimeSeries
    : public MultiLevelTimeSeries<
          T,
          folly::LegacyStatsClock<std::chrono::seconds>> {
 public:
  enum Levels {
    FIVE_SECOND,
    TEN_SECOND,
    TWENTY_SECOND,
    THIRTY_SECOND,
    MINUTE,
    NUM_LEVELS,
  };

  HistogramSubminuteMinuteOnlyTimeSeries()
      : MultiLevelTimeSeries<T, folly::LegacyStatsClock<std::chrono::seconds>>(
            NUM_LEVELS,
            60,
            kHistogramSubminuteMinuteOnlyDurations) {}
};

const std::chrono::seconds kHistogramMinuteOnlyDurations[] = {
    std::chrono::seconds(60)};

template <class T>
class HistogramMinuteOnlyTimeSeries
    : public MultiLevelTimeSeries<
          T,
          folly::LegacyStatsClock<std::chrono::seconds>> {
 public:
  enum Levels {
    MINUTE,
    NUM_LEVELS,
  };

  HistogramMinuteOnlyTimeSeries()
      : MultiLevelTimeSeries<T, folly::LegacyStatsClock<std::chrono::seconds>>(
            NUM_LEVELS,
            60,
            kHistogramMinuteOnlyDurations) {}
};

const std::chrono::seconds kHistogramMinuteTenMinuteOnlyDurations[] = {
    std::chrono::seconds(60),
    std::chrono::seconds(600)};

template <class T>
class HistogramMinuteTenMinuteOnlyTimeSeries
    : public MultiLevelTimeSeries<
          T,
          folly::LegacyStatsClock<std::chrono::seconds>> {
 public:
  enum Levels {
    MINUTE,
    TEN_MINUTE,
    NUM_LEVELS,
  };

  HistogramMinuteTenMinuteOnlyTimeSeries()
      : MultiLevelTimeSeries<T, folly::LegacyStatsClock<std::chrono::seconds>>(
            NUM_LEVELS,
            60,
            kHistogramMinuteTenMinuteOnlyDurations) {}
};

/**
 * TimeseriesHistogram is a class which allows you to track data distributions
 * as they change over time.
 *
 * Specifically, it is a bucketed histogram with different value ranges
 * assigned to each bucket.  Within each bucket is a MultiLevelTimeSeries as
 * from 'common/stats/Timeseries.h'. This means that each bucket contains a
 * different set of data for different historical time periods, and one can
 * query data distributions over different trailing time windows.
 *
 * For example, this can answer questions: "What is the data distribution over
 * the last minute? Over the last 10 minutes?  Since I last cleared this
 * histogram?"
 *
 * The class can also estimate percentiles and answer questions like:
 *
 *   "What was the 99th percentile data value over the last 10 minutes?"
 *
 * However, note that depending on the size of your buckets and the smoothness
 * of your data distribution, the estimate may be way off from the actual
 * value.  In particular, if the given percentile falls outside of the bucket
 * range (i.e. your buckets range in 0 - 100,000 but the 99th percentile is
 * around 115,000) this estimate may be very wrong.
 *
 * The memory usage for a typical histogram is roughly 3k * (# of buckets).  All
 * insertion operations are amortized O(1), and all queries are O(# of buckets).
 */

template <class T>
class TimeseriesHistogram
    : public folly::TimeseriesHistogram<
          T,
          folly::LegacyStatsClock<std::chrono::seconds>,
          MultiLevelTimeSeries<
              T,
              folly::LegacyStatsClock<std::chrono::seconds>>> {
 public:
  // values to be inserted into container
  using ValueType = T;
  // the container type we use internally for each bucket
  using ContainerType =
      MultiLevelTimeSeries<T, folly::LegacyStatsClock<std::chrono::seconds>>;
  // The parent type
  using BaseType = folly::TimeseriesHistogram<
      T,
      folly::LegacyStatsClock<std::chrono::seconds>,
      MultiLevelTimeSeries<T, folly::LegacyStatsClock<std::chrono::seconds>>>;
  // The time type.
  using TimeType = typename BaseType::Duration;

  /**
   * Creates a TimeSeries histogram and initializes the bucketing and levels.
   *
   * The buckets are created by chopping the range [min, max) into pieces
   * of size bucketSize, with the last bucket being potentially shorter.  Two
   * additional buckets are always created -- the "under" bucket for the range
   * (-inf, min) and the "over" bucket for the range [max, +inf).
   *
   * By default, the histogram will use levels of 60/600/3600/alltime (seconds),
   * but his can be overridden by passing in an already-constructed multilevel
   * timeseries with the desired level durations.
   *
   * @param bucketSize the width of each bucket
   * @param min the smallest value for the bucket range.
   * @param max the largest value for the bucket range
   * @param defaultContainer a pre-initialized timeseries with the desired
   *                         number of levels and their durations.
   */
  TimeseriesHistogram(
      ValueType bucketSize,
      ValueType min,
      ValueType max,
      const ContainerType& defaultContainer =
          HistogramMinuteTenMinuteHourTimeSeries<T>())
      : BaseType(bucketSize, min, max, defaultContainer) {}

  /**
   * Create a timeseries histogram from the default (millisecond) timeseries.
   * It is a common pattern for using the same default timeseries template
   * to initialize both timeseries and histogram, which use different clock (for
   * now).
   * TODO(lupan): delete this constructor when histogram is also migrated to
   * use millisecond clock.
   */
  TimeseriesHistogram(
      ValueType bucketSize,
      ValueType min,
      ValueType max,
      const MultiLevelTimeSeries<T>& defaultContainer)
      : BaseType(bucketSize, min, max, ContainerType{defaultContainer}) {}

  /**
   * Updates every underlying timeseries object with the given timestamp. You
   * must call this directly before querying to ensure that the data in all
   * buckets is decayed properly.
   */
  void update(time_t now) {
    BaseType::update(std::chrono::seconds(now));
  }

  // Inherit the folly::TimeseriesHistogram versions of addValue() and
  // addValues() too
  using BaseType::addValue;
  using BaseType::addValues;

  /** Adds a value into the histogram with timestamp 'now' */
  void addValue(time_t now, const ValueType& value) {
    BaseType::addValue(std::chrono::seconds(now), value);
  }

  /** Adds a value the given number of times with timestamp 'now' */
  void addValue(time_t now, const ValueType& value, int64_t times) {
    BaseType::addValue(std::chrono::seconds(now), value, times);
  }

  /*
   * Adds all of the values from the specified histogram.
   *
   * All of the values will be added to the current time-slot.
   *
   * One use of this is for thread-local caching of frequently updated
   * histogram data.  For example, each thread can store a thread-local
   * Histogram that is updated frequently, and only add it to the global
   * TimeseriesHistogram once a second.
   */
  void addValues(time_t now, const folly::Histogram<ValueType>& values) {
    BaseType::addValues(std::chrono::seconds(now), values);
  }

  /** Prints out the whole histogram timeseries in human-readable form */
  std::string debugString() const;
};

} // namespace facebook::fb303

#include <fb303/TimeseriesHistogram-inl.h>
