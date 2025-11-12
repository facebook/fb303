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

#include <fb303/ExportType.h>
#include <fb303/Timeseries.h>
#include <folly/Synchronized.h>
#include <chrono>

namespace facebook::fb303 {

class DynamicCounters;
using CounterType = int64_t;
using ExportedStat = MultiLevelTimeSeries<CounterType>;

class TimeseriesExporter {
 public:
  using StatPtr = std::shared_ptr<folly::Synchronized<ExportedStat>>;

  /**
   * Register the counter callback with the DynamicCounters object.
   */
  static void exportStat(
      const StatPtr& stat,
      ExportType type,
      folly::StringPiece statName,
      DynamicCounters* counters);

  /**
   * Register the counter callback with the DynamicCounters object.
   *
   * If updateOnRead is false, the stat will not be updated
   * when processing read queries.
   */
  static void exportStat(
      const StatPtr& stat,
      ExportType type,
      folly::StringPiece statName,
      DynamicCounters* counters,
      bool updateOnRead);

  /**
   * Unregister the counter callback from the DynamicCounters object.
   */
  static void unExportStat(
      const StatPtr& stat,
      ExportType type,
      folly::StringPiece statName,
      DynamicCounters* counters);

  /**
   * Compute the counter name from the given type and level and copy
   * into counterName. The counterName buffer will always be
   * null-terminated, even if counterNameSize is too small to hold the
   * entire output.
   */
  template <typename MLTS>
  static void getCounterName(
      char* counterName,
      const int counterNameSize,
      const MLTS* stat,
      folly::StringPiece statName,
      ExportType type,
      const int level) {
    // NOTE:  We access the stat object here without locking.  This depends
    // on the fact that getLevel(), and Level::isAllTime() and
    // Level::duration() are all non-volatile calls meaning they only read
    // things that are constant once the stat is constructed (number of levels
    // can never change, nor their durations).
    //   - mrabkin
    if (stat->getLevel(level).isAllTime()) {
      // typical name: 'ad_request.rate' or 'ad_request_elapsed_time.avg'
      snprintf(
          counterName,
          counterNameSize,
          "%.*s.%s",
          static_cast<int>(statName.size()),
          statName.data(),
          TimeseriesExporter::getTypeString()[type]);
    } else {
      // typical name: 'ad_request.rate.600' or
      // 'ad_request_elapsed_time.avg.3600'
      auto duration = stat->getLevel(level).duration();
      auto durationSecs =
          std::chrono::duration_cast<std::chrono::seconds>(duration);
      DCHECK(
          std::chrono::duration_cast<typename MLTS::Duration>(durationSecs) ==
          duration);
      snprintf(
          counterName,
          counterNameSize,
          "%.*s.%s.%ld",
          static_cast<int>(statName.size()),
          statName.data(),
          TimeseriesExporter::getTypeString()[type],
          static_cast<long>(durationSecs.count()));
    }
  }

  static std::array<const char* const, 5> getTypeString();

 private:
  /*
   * Get the specified export value from the specified timeseries level.
   *
   * This method also updates the stat with the current time (stats will not
   * decay properly without this if no new items are being inserted)
   */
  static CounterType
  getStatValue(ExportedStat& stat, ExportType type, int level);

  /*
   * Get the specified export value from the specified timeseries level.
   *
   * This method also updates the stat with the current time (stats will not
   * decay properly without this if no new items are being inserted)
   *
   * If update is true, calls the update() method on the stat
   * before querying the value.
   */
  static CounterType
  getStatValue(ExportedStat& stat, ExportType type, int level, bool update);
};
} // namespace facebook::fb303
