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

#include <folly/stats/BucketedTimeSeries.h>
#include <folly/stats/MultiLevelTimeSeries.h>

namespace facebook::fb303 {

template <typename T, typename CT>
class MultiLevelTimeSeries;

namespace detail {
template <typename Duration, typename ValueType, typename CLOCK>
std::vector<Duration> getDurations(
    const MultiLevelTimeSeries<ValueType, CLOCK>& timeseries) {
  std::vector<Duration> res;
  size_t levels = timeseries.numLevels();
  res.reserve(levels);
  for (size_t i = 0; i < levels; ++i) {
    typename CLOCK::duration original = timeseries.getLevel(i).duration();
    Duration duration = std::chrono::duration_cast<Duration>(original);
    DCHECK(
        std::chrono::duration_cast<typename CLOCK::duration>(duration) ==
        original)
        << "precision loss when converting MultiLevelTimeSeries to a different clock type";
    res.push_back(duration);
  }
  return res;
}
} // namespace detail

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
template <
    class T,
    typename CT = folly::LegacyStatsClock<std::chrono::milliseconds>>
class MultiLevelTimeSeries : public folly::MultiLevelTimeSeries<T, CT> {
 public:
  using BaseType = folly::MultiLevelTimeSeries<T, CT>;
  using Duration = typename BaseType::Duration;
  using TimePoint = typename BaseType::TimePoint;
  // The legacy TimeType.  The older code used this instead of Duration and
  // TimePoint.  This will eventually be removed as the code is transitioned to
  // Duration and TimePoint.
  using TimeType = typename BaseType::Duration;

  explicit MultiLevelTimeSeries(
      size_t num_buckets,
      folly::Range<const Duration*> durations)
      : BaseType(num_buckets, durations) {}

  explicit MultiLevelTimeSeries(
      size_t num_levels,
      size_t num_buckets,
      const Duration* durations)
      : MultiLevelTimeSeries(num_buckets, folly::Range(durations, num_levels)) {
  }

  /*
   * Convert between MultiLevelTimeSeries of the same value type but different
   * clock type.
   */
  template <typename CLOCK>
  explicit MultiLevelTimeSeries(
      const MultiLevelTimeSeries<T, CLOCK>& timeseries)
      : MultiLevelTimeSeries(
            timeseries.numBuckets(),
            detail::getDurations<Duration>(timeseries)) {}

  /* convert all the rate to std::chrono::seconds to be backwards compatible */
  template <typename ReturnType = double>
  ReturnType rate(size_t level) const {
    return BaseType::template rate<ReturnType, std::chrono::seconds>(level);
  }

  template <typename ReturnType = double>
  ReturnType rate(Duration duration) const {
    return BaseType::template rate<ReturnType, std::chrono::seconds>(duration);
  }

  template <typename ReturnType = double>
  ReturnType rate(TimePoint start, TimePoint end) const {
    return BaseType::template rate<ReturnType, std::chrono::seconds>(
        start, end);
  }

  template <typename ReturnType = double>
  ReturnType countRate(size_t level) const {
    return BaseType::template countRate<ReturnType, std::chrono::seconds>(
        level);
  }

  template <typename ReturnType = double>
  ReturnType countRate(Duration duration) const {
    return BaseType::template countRate<ReturnType, std::chrono::seconds>(
        duration);
  }
};

const std::chrono::milliseconds kMinuteDurations[] = {
    std::chrono::seconds(60),
    std::chrono::seconds(0)};

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

const std::chrono::milliseconds kMinuteHourDurations[] = {
    std::chrono::seconds(60),
    std::chrono::seconds(3600),
    std::chrono::seconds(0)};

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

const std::chrono::milliseconds kMinuteTenMinuteHourDurations[] = {
    std::chrono::seconds(60),
    std::chrono::seconds(600),
    std::chrono::seconds(3600),
    std::chrono::seconds(0)};

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

const std::chrono::milliseconds kMinuteHourDayDurations[] = {
    std::chrono::seconds(60),
    std::chrono::seconds(3600),
    std::chrono::seconds(86400),
    std::chrono::seconds(0)};

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

const std::chrono::milliseconds kMinuteTenMinuteDurations[] = {
    std::chrono::seconds(60),
    std::chrono::seconds(600),
    std::chrono::seconds(0)};

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

const std::chrono::milliseconds kTenMinuteHourDurations[] = {
    std::chrono::seconds(600),
    std::chrono::seconds(3600)};

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

const std::chrono::milliseconds kQuarterMinuteOnlyDurations[] = {
    std::chrono::seconds(15)};

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

const std::chrono::milliseconds kMinuteOnlyDurations[] = {
    std::chrono::seconds(60)};

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

const std::chrono::milliseconds kTenMinuteOnlyDurations[] = {
    std::chrono::seconds(600)};

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

const std::chrono::milliseconds kMinuteTenMinuteOnlyDurations[] = {
    std::chrono::seconds(60),
    std::chrono::seconds(600)};

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

const std::chrono::milliseconds kHourDurations[] = {
    std::chrono::seconds(3600),
    std::chrono::seconds(0)};

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

const std::chrono::milliseconds kTenMinutesChunksDurations[] = {
    std::chrono::seconds(600),
    std::chrono::seconds(1200),
    std::chrono::seconds(1800)};

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

const std::chrono::milliseconds kSubminuteMinuteDurations[] = {
    std::chrono::seconds(5),
    std::chrono::seconds(10),
    std::chrono::seconds(20),
    std::chrono::seconds(30),
    std::chrono::seconds(60),
    std::chrono::seconds(0)};

template <class T>
class SubminuteMinuteTimeSeries : public MultiLevelTimeSeries<T> {
 public:
  enum Levels {
    FIVE_SECOND,
    TEN_SECOND,
    TWENTY_SECOND,
    THIRTY_SECOND,
    MINUTE,
    ALLTIME,
    NUM_LEVELS,
  };

  SubminuteMinuteTimeSeries()
      : MultiLevelTimeSeries<T>(NUM_LEVELS, 60, kSubminuteMinuteDurations) {}
};

const std::chrono::milliseconds kSubminuteMinuteOnlyDurations[] = {
    std::chrono::seconds(5),
    std::chrono::seconds(10),
    std::chrono::seconds(20),
    std::chrono::seconds(30),
    std::chrono::seconds(60)};

template <class T>
class SubminuteMinuteOnlyTimeSeries : public MultiLevelTimeSeries<T> {
 public:
  enum Levels {
    FIVE_SECOND,
    TEN_SECOND,
    TWENTY_SECOND,
    THIRTY_SECOND,
    MINUTE,
    NUM_LEVELS,
  };

  SubminuteMinuteOnlyTimeSeries()
      : MultiLevelTimeSeries<T>(NUM_LEVELS, 60, kSubminuteMinuteOnlyDurations) {
  }
};

} // namespace facebook::fb303
