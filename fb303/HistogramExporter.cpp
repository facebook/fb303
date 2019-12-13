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

#include <fb303/HistogramExporter.h>

#include <fb303/ExportedHistogramMapImpl.h>
#include <fb303/LegacyClock.h>
#include <fb303/TimeseriesExporter.h>
#include <folly/Format.h>
#include <folly/Optional.h>
#include <folly/String.h>
#include <folly/small_vector.h>
#include <glog/logging.h>
#include <functional>

using folly::format;
using folly::Optional;
using folly::StringPiece;
using std::shared_ptr;
using std::string;
using std::chrono::duration_cast;

namespace facebook {
namespace fb303 {

static std::string getHistogramBuckets(const HistogramPtr& hist, int level) {
  CHECK(hist);

  auto lockedHist = hist->lock();

  // make sure the histogram is up to date and data is decayed appropriately
  lockedHist->update(get_legacy_stats_time());

  // return the serialized bucket info
  return lockedHist->getString(level);
}

static CounterType
getHistogramStat(const HistogramPtr& item, int level, ExportType exportType) {
  auto lockedHist = item->lock();

  // make sure the histogram is up to date and data is decayed appropriately
  lockedHist->update(get_legacy_stats_time());

  switch (exportType) {
    case SUM:
      return lockedHist->sum(level);
    case COUNT:
      return lockedHist->count(level);
    case AVG:
      return lockedHist->avg<CounterType>(level);
    case RATE:
      return lockedHist->rate<CounterType>(level);
    case PERCENT:
      return static_cast<CounterType>(100.0 * lockedHist->avg<double>(level));
  }
  // The default case is explicitly handled outside of the switch statement
  // so gcc's -Wswitch warning will complain if a new export type is added
  // and this switch statement is not updated.

  LOG(DFATAL) << "invalid export type: " << exportType;
  return 0;
}

/* static */
void HistogramExporter::exportBuckets(
    const HistogramPtr& hist,
    StringPiece name,
    DynamicStrings* strings) {
  CHECK(hist);
  CHECK(strings);

  // All the buckets in the histogram are guaranteed to have the same number
  // of levels and the same level durations, so we grab the first bucket.
  //
  // NOTE:  We access the histogram's stat object here without locking.  This
  // depends on the fact that getLevel(), and Level::isAllTime() and
  // Level::duration() are all non-volatile calls meaning they only read
  // things that are constant once the stat is constructed (number of levels
  // can never change, nor their durations).
  //
  //   - mrabkin
  CHECK_GT(hist->lock()->getNumBuckets(), 0);
  const ExportedHistogram::ContainerType& stat = hist->lock()->getBucket(0);

  // now, export each level
  for (size_t level = 0; level < stat.numLevels(); ++level) {
    std::string valueName;
    if (stat.getLevel(level).isAllTime()) {
      // example name: ad_request_elapsed_time.hist
      valueName = sformat("{}.hist", name);
    } else {
      // example name: ad_request_elapsed_time.hist.600
      const auto durationSecs =
          duration_cast<std::chrono::seconds>(stat.getLevel(level).duration());
      valueName = sformat("{}.hist.{}", name, durationSecs.count());
    }
    // get the stat function callback and put it in a wrapper that will grab
    // the necessary lock and call update() on the stat
    // register the actual counter callback
    strings->registerCallback(
        valueName, [=] { return getHistogramBuckets(hist, level); });
  }
}

/* static */
template <typename Fn>
void HistogramExporter::forEachPercentileName(
    const HistogramPtr& hist,
    StringPiece name,
    int percentile,
    const Fn& fn) {
  CHECK_GT(hist->lock()->getNumBuckets(), 0);
  CHECK_GE(percentile, 0);
  CHECK_LE(percentile, 100);

  const ExportedHistogram::ContainerType& stat = hist->lock()->getBucket(0);
  for (size_t level = 0; level < stat.numLevels(); ++level) {
    // NOTE:  We access the histogram's stat object here without locking.  This
    // depends on the fact that getLevel(), and Level::isAllTime() and
    // Level::duration() are all non-volatile calls meaning they only read
    // things that are constant once the stat is constructed (number of levels
    // can never change, nor their durations).
    //   - mrabkin

    std::string counterName;
    if (stat.getLevel(level).isAllTime()) {
      // example name: ad_request_elapsed_time.p95
      counterName = sformat("{}.p{}", name, percentile);
    } else {
      // example name: ad_request_elapsed_time.p95.600
      const auto durationSecs =
          duration_cast<std::chrono::seconds>(stat.getLevel(level).duration());
      counterName =
          sformat("{}.p{}.{}", name, percentile, durationSecs.count());
    }

    fn(counterName, level);
  }
}

/* static */
void HistogramExporter::exportPercentile(
    const HistogramPtr& hist,
    StringPiece name,
    int percentile,
    DynamicCounters* counters) {
  forEachPercentileName(
      hist, name, percentile, [&](StringPiece counterName, int level) {
        counters->registerCallback(counterName, [=] {
          return getHistogramPercentile(hist, level, percentile);
        });
      });
}

/* static */
void HistogramExporter::unexportPercentile(
    const HistogramPtr& hist,
    StringPiece name,
    int percentile,
    DynamicCounters* counters) {
  forEachPercentileName(
      hist, name, percentile, [&](StringPiece counterName, int /* unused */) {
        counters->unregisterCallback(counterName);
      });
}

/* static */
template <typename Fn>
void HistogramExporter::forEachStatName(
    const HistogramPtr& hist,
    StringPiece name,
    ExportType exportType,
    const Fn& fn) {
  const size_t kNameSize = name.size() + 50; // some extra space
  folly::small_vector<char, 200> counterName(kNameSize);

  const ExportedHistogram::ContainerType& stat = hist->lock()->getBucket(0);
  for (size_t level = 0; level < stat.numLevels(); ++level) {
    TimeseriesExporter::getCounterName(
        counterName.data(), kNameSize, &stat, name, exportType, level);
    fn(counterName.data(), level);
  }
}

/* static */
void HistogramExporter::exportStat(
    const HistogramPtr& hist,
    StringPiece name,
    ExportType exportType,
    DynamicCounters* counters) {
  forEachStatName(
      hist, name, exportType, [&](StringPiece counterName, int level) {
        counters->registerCallback(counterName, [=] {
          return getHistogramStat(hist, level, exportType);
        });
      });
}

/* static */
void HistogramExporter::unexportStat(
    const HistogramPtr& hist,
    StringPiece name,
    ExportType exportType,
    DynamicCounters* counters) {
  forEachStatName(
      hist, name, exportType, [&](StringPiece counterName, int /* unused */) {
        counters->unregisterCallback(counterName);
      });
}
} // namespace fb303
} // namespace facebook
