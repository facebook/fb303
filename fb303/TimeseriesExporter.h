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

#include <fb303/ExportType.h>
#include <fb303/MutexWrapper.h>
#include <fb303/SynchMap.h>
#include <fb303/Timeseries.h>

namespace facebook {
namespace fb303 {

class DynamicCounters;
using CounterType = int64_t;
using ExportedStat = MultiLevelTimeSeries<CounterType>;

class TimeseriesExporter {
 public:
  using StatPtr =
      std::shared_ptr<folly::Synchronized<ExportedStat, MutexWrapper>>;

  /**
   * Register the counter callback with the DynamicCounters object.
   */
  static void exportStat(
      const StatPtr& stat,
      ExportType type,
      folly::StringPiece statName,
      DynamicCounters* counters);

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
  static void getCounterName(
      char* counterName,
      const int counterNameSize,
      const ExportedStat* stat,
      folly::StringPiece statName,
      ExportType type,
      const int level);

 private:
  /*
   * Get the specified export value from the specified timeseries level.
   *
   * This method also updates the stat with the current time (stats will not
   * decay properly without this if no new items are being inserted)
   */
  static CounterType
  getStatValue(ExportedStat& stat, ExportType type, int level);
};
} // namespace fb303
} // namespace facebook
