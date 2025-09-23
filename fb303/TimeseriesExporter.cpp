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

#include <fb303/TimeseriesExporter.h>

#include <fb303/DynamicCounters.h>
#include <fb303/LegacyClock.h>
#include <folly/small_vector.h>
#include <glog/logging.h>

using folly::StringPiece;
using std::chrono::duration_cast;

namespace facebook::fb303 {

std::array<const char* const, 5> kTypeString = {{
    "sum",
    "count",
    "avg",
    "rate",
    "pct",
}};

/* static */
std::array<const char* const, 5> TimeseriesExporter::getTypeString() {
  return kTypeString;
}

/* static */
CounterType TimeseriesExporter::getStatValue(
    ExportedStat& stat,
    ExportType type,
    int level) {
  return getStatValue(stat, type, level, true /* update */);
}

/* static */
CounterType TimeseriesExporter::getStatValue(
    ExportedStat& stat,
    ExportType type,
    int level,
    bool update) {
  // update the stat with the current time -- if no new items are being
  // inserted, the stats won't decay properly without this update()
  if (update) {
    stat.update(
        ExportedStat::TimePoint(std::chrono::seconds(get_legacy_stats_time())));
  }

  // retrieve the correct type of info from the stat
  switch (type) {
    case SUM:
      return stat.sum(level);
    case AVG:
      return stat.avg<CounterType>(level);
    case RATE:
      return stat.rate<CounterType>(level);
    case PERCENT:
      return static_cast<CounterType>(100.0 * stat.avg<double>(level));
    case COUNT:
      // getCount() returns int64_t, so we cast it to CounterType to be safe
      return static_cast<CounterType>(stat.count(level));
  }
  // We intentionally avoid having a default switch statement so gcc's
  // -Wswitch flag will warn if we do not handle all enum values here.

  LOG(FATAL) << "invalid stat export type: " << type;
}

/* static */
void TimeseriesExporter::exportStat(
    const StatPtr& stat,
    ExportType type,
    StringPiece statName,
    DynamicCounters* counters) {
  return exportStat(stat, type, statName, counters, true /* updateOnRead */);
}

/* static */
void TimeseriesExporter::exportStat(
    const StatPtr& stat,
    ExportType type,
    StringPiece statName,
    DynamicCounters* counters,
    bool updateOnRead) {
  CHECK_GE(type, 0);
  CHECK_LT(type, ExportTypeMeta::kNumExportTypes);

  const size_t kNameSize = statName.size() + 50; // some extra space
  folly::small_vector<char, 200> counterName(kNameSize);

  // statObj is used below just to get levels info. As level info is set just
  // in the construction, it's safe to use it without a lock
  auto& statObj = stat->unsafeGetUnlocked();
  for (size_t lev = 0; lev < statObj.numLevels(); ++lev) {
    getCounterName(
        counterName.data(), kNameSize, &statObj, statName, type, lev);

    // register the actual counter callback with the DynamicCounters obj, if it
    // hasn't already been registered.
    counters->registerCallback(
        counterName.data(),
        [=] { return getStatValue(*stat->lock(), type, lev, updateOnRead); },
        /* overwrite */ false);
  }
}

/* static */
void TimeseriesExporter::unExportStat(
    const StatPtr& stat,
    ExportType type,
    StringPiece statName,
    DynamicCounters* counters) {
  CHECK_GE(type, 0);
  CHECK_LT(type, ExportTypeMeta::kNumExportTypes);

  const size_t kNameSize = statName.size() + 50; // some extra space
  folly::small_vector<char, 200> counterName(kNameSize);

  auto statObj = stat->lock().operator->();
  for (size_t lev = 0; lev < stat->lock()->numLevels(); ++lev) {
    getCounterName(counterName.data(), kNameSize, statObj, statName, type, lev);

    // unregister the counter callback from the DynamicCounters obj
    counters->unregisterCallback(counterName.data());
  }
}
} // namespace facebook::fb303
