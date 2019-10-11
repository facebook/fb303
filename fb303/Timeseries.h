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

#include <folly/stats/BucketedTimeSeries.h>
#include <folly/stats/MultiLevelTimeSeries.h>

namespace facebook {
namespace fb303 {

/** class MultiLevelTimeSeries
 *
 * This class has been moved to folly/stats/MultiLevelTimeSeries.h. This is left
 * here to provide backward compatibility. If you are writing new code, consider
 * using folly's implementation directly.
 *
 * This class represents a timeseries which keeps several levels of data
 * granularity (similar in principle to the loads reported by the UNIX
 * 'uptime' command).  It uses several instances (one per level) of
 * BucketedTimeSeries as the underlying storage.
 *
 * This can easily be used to track sums (and thus rates or averages) over
 * several predetermined time periods, as well as all-time sums.  For example,
 * you would use to it to track query rate or response speed over the last
 * 5, 15, 30, and 60 minutes.
 *
 * The MultiLevelTimeSeries takes a list of level durations as an input; the
 * durations must be strictly increasing.  Furthermore a special level can be
 * provided with a duration of '0' -- this will be an "all-time" level.  If
 * an all-time level is provided, it MUST be the last level present.
 *
 * The class assumes that time advances forward --  you can't retroactively add
 * values for events in the past -- the 'now' argument is provided for better
 * efficiency and ease of unittesting.
 *
 * The class is not thread-safe -- use your own synchronization!
 *
 * @author Mark Rabkin (mrabkin@facebook.com)
 */
template <class T>
class MultiLevelTimeSeries : public folly::MultiLevelTimeSeries<T> {
 public:
  using BaseType = folly::MultiLevelTimeSeries<T>;
  using Duration = typename BaseType::Duration;
  using TimePoint = typename BaseType::TimePoint;
  // The legacy TimeType.  The older code used this instead of Duration and
  // TimePoint.  This will eventually be removed as the code is transitioned to
  // Duration and TimePoint.
  using TimeType = typename BaseType::Duration;

  MultiLevelTimeSeries(
      int num_levels,
      int num_buckets,
      const int* level_durations);

  void update(time_t now) {
    BaseType::update(convertTimeT(now));
  }

  using BaseType::addValue;
  using BaseType::addValueAggregated;
  using BaseType::update;

  void addValue(time_t now, const T& val) {
    BaseType::addValue(convertTimeT(now), val);
  }

  void addValue(time_t now, const T& val, int64_t times) {
    BaseType::addValue(convertTimeT(now), val, times);
  }

  void addValueAggregated(time_t now, const T& total, int64_t nsamples) {
    BaseType::addValueAggregated(convertTimeT(now), total, nsamples);
  }

 private:
  static TimePoint convertTimeT(time_t t) {
    auto sinceEpoch =
        std::chrono::duration_cast<TimeType>(std::chrono::seconds(t));
    return TimePoint(sinceEpoch);
  }
};

const int kMinuteDurations[] = {60, 0};

template <class T>
class MinuteTimeSeries : public MultiLevelTimeSeries<T> {
 public:
  enum Levels {
    MINUTE,
    ALLTIME,
    NUM_LEVELS,
  };

  MinuteTimeSeries()
      : MultiLevelTimeSeries<T>(NUM_LEVELS, 60, kMinuteDurations) {}
};

const int kMinuteHourDurations[] = {60, 3600, 0};

template <class T>
class MinuteHourTimeSeries : public MultiLevelTimeSeries<T> {
 public:
  enum Levels {
    MINUTE,
    HOUR,
    ALLTIME,
    NUM_LEVELS,
  };

  MinuteHourTimeSeries()
      : MultiLevelTimeSeries<T>(NUM_LEVELS, 60, kMinuteHourDurations) {}
};

const int kMinuteTenMinuteHourDurations[] = {60, 600, 3600, 0};

template <class T>
class MinuteTenMinuteHourTimeSeries : public MultiLevelTimeSeries<T> {
 public:
  enum Levels {
    MINUTE,
    TEN_MINUTE,
    HOUR,
    ALLTIME,
    NUM_LEVELS,
  };

  MinuteTenMinuteHourTimeSeries()
      : MultiLevelTimeSeries<T>(NUM_LEVELS, 60, kMinuteTenMinuteHourDurations) {
  }
};

const int kMinuteHourDayDurations[] = {60, 3600, 86400, 0};

template <class T>
class MinuteHourDayTimeSeries : public MultiLevelTimeSeries<T> {
 public:
  enum Levels {
    MINUTE,
    HOUR,
    DAY,
    ALLTIME,
    NUM_LEVELS,
  };

  MinuteHourDayTimeSeries()
      : MultiLevelTimeSeries<T>(NUM_LEVELS, 60, kMinuteHourDayDurations) {}
};

const int kMinuteTenMinuteDurations[] = {60, 600, 0};

template <class T>
class MinuteTenMinuteTimeSeries : public MultiLevelTimeSeries<T> {
 public:
  enum Levels {
    MINUTE,
    TEN_MINUTE,
    ALLTIME,
    NUM_LEVELS,
  };

  MinuteTenMinuteTimeSeries()
      : MultiLevelTimeSeries<T>(NUM_LEVELS, 60, kMinuteTenMinuteDurations) {}
};

const int kTenMinuteHourDurations[] = {600, 3600};

template <class T>
class TenMinuteHourTimeSeries : public MultiLevelTimeSeries<T> {
 public:
  enum Levels {
    TEN_MINUTE,
    HOUR,
    NUM_LEVELS,
  };

  TenMinuteHourTimeSeries()
      : MultiLevelTimeSeries<T>(NUM_LEVELS, 60, kTenMinuteHourDurations) {}
};

const int kQuarterMinuteOnlyDurations[] = {15};

template <class T>
class QuarterMinuteOnlyTimeSeries : public MultiLevelTimeSeries<T> {
 public:
  enum Levels {
    QUARTER_MINUTE,
    NUM_LEVELS,
  };

  QuarterMinuteOnlyTimeSeries()
      : MultiLevelTimeSeries<T>(NUM_LEVELS, 15, kQuarterMinuteOnlyDurations) {}
};

const int kMinuteOnlyDurations[] = {60};

template <class T>
class MinuteOnlyTimeSeries : public MultiLevelTimeSeries<T> {
 public:
  enum Levels {
    MINUTE,
    NUM_LEVELS,
  };

  MinuteOnlyTimeSeries()
      : MultiLevelTimeSeries<T>(NUM_LEVELS, 60, kMinuteOnlyDurations) {}
};

const int kTenMinuteOnlyDurations[] = {600};

template <class T>
class TenMinuteOnlyTimeSeries : public MultiLevelTimeSeries<T> {
 public:
  enum Levels {
    TEN_MINUTE,
    NUM_LEVELS,
  };

  TenMinuteOnlyTimeSeries()
      : MultiLevelTimeSeries<T>(NUM_LEVELS, 60, kTenMinuteOnlyDurations) {}
};

const int kMinuteTenMinuteOnlyDurations[] = {60, 600};

template <class T>
class MinuteTenMinuteOnlyTimeSeries : public MultiLevelTimeSeries<T> {
 public:
  enum Levels {
    MINUTE,
    TEN_MINUTE,
    NUM_LEVELS,
  };

  MinuteTenMinuteOnlyTimeSeries()
      : MultiLevelTimeSeries<T>(NUM_LEVELS, 60, kMinuteTenMinuteOnlyDurations) {
  }
};

const int kHourDurations[] = {3600, 0};

template <class T>
class HourTimeSeries : public MultiLevelTimeSeries<T> {
 public:
  enum Levels {
    HOUR,
    ALLTIME,
    NUM_LEVELS,
  };

  HourTimeSeries() : MultiLevelTimeSeries<T>(NUM_LEVELS, 60, kHourDurations) {}
};

const int kTenMinutesChunksDurations[] = {600, 1200, 1800};

template <class T>
class TenMinutesChunksTimeSeries : public MultiLevelTimeSeries<T> {
 public:
  enum Levels {
    TEN_MINUTES,
    TWENTY_MINUTES,
    THIRTY_MINUTES,
    NUM_LEVELS,
  };

  TenMinutesChunksTimeSeries()
      : MultiLevelTimeSeries<T>(NUM_LEVELS, 60, kTenMinutesChunksDurations) {}
};
} // namespace fb303
} // namespace facebook

#include <fb303/Timeseries-inl.h>
