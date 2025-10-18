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

#include <fb303/ExportedHistogramMap.h>
#include <fb303/HistogramExporter.h>
#include <fb303/LegacyClock.h>

using folly::StringPiece;

namespace facebook::fb303 {

CounterType getHistogramPercentile(
    const ExportedHistogramMap::HistogramPtr& hist,
    int level,
    double percentile) {
  auto lockedHist = hist->wlock();

  // make sure the histogram is up to date and data is decayed appropriately
  lockedHist->update(get_legacy_stats_time());

  // return the estimated percentile value for the given percentile
  return lockedHist->getPercentileEstimate(percentile, level);
}

ExportedHistogramMap::ExportedHistogramMap(
    DynamicCounters* counters,
    DynamicStrings* strings,
    const ExportedHistogram& copyMe)
    : dynamicCounters_(counters),
      dynamicStrings_(strings),
      defaultHist_(std::make_shared<ExportedHistogram>(copyMe)) {}

ExportedHistogramMap::HistogramPtr ExportedHistogramMap::getOrCreateUnlocked(
    StringPiece name,
    bool& wasCreated,
    MakeExportedHistogram makeExportedHistogram) {
  wasCreated = false;

  auto hist = getHistogramUnlocked(name);
  if (hist != nullptr) {
    return hist;
  }

  auto value = std::make_shared<SyncHistogram>(makeExportedHistogram());

  bool inserted;
  {
    // The returned iterator may be invalidated by a concurrent insert, so it
    // must be dereferenced before releasing the lock guard.
    HistMap::value_type toInsert(name, std::move(value));
    auto lockedHistMap = histMap_.wlock();
    auto item = lockedHistMap->insert(std::move(toInsert));
    inserted = item.second;
    hist = item.first->second;
    CHECK(hist);
  }

  if (inserted) {
    HistogramExporter::exportBuckets(hist, name, dynamicStrings_);
  }
  wasCreated = inserted;

  return hist;
}

bool ExportedHistogramMap::addHistogram(
    StringPiece name,
    const ExportedHistogram& copyMe) {
  // Call getOrCreateUnlocked() to do all of the work.
  bool created = false;
  auto item = getOrCreateUnlocked(name, created, [&] {
    ExportedHistogram res = copyMe;
    res.clear();
    return res;
  });
  if (!created) {
    checkAdd(
        name, item, copyMe.getBucketSize(), copyMe.getMin(), copyMe.getMax());
  }
  return created;
}

bool ExportedHistogramMap::addHistogram(
    StringPiece name,
    int64_t bucketWidth,
    int64_t min,
    int64_t max) {
  HistogramPtr newHistogram;

  // Acquire the lock while attempting the insert.
  {
    auto lockedHistMap = histMap_.wlock();

    // Call emplace() with a null HistogramPtr first.  Creating a new histogram
    // object is somewhat expensive, so wait to create it until we know we'll
    // need it.
    auto ret = lockedHistMap->emplace(name, nullptr);
    if (!ret.second) {
      // The histogram already existed.
      return false;
    }

    // We inserted a new entry.  We now need to create and export the
    // histogram.
    //
    // In the unlikely case that anything goes wrong after we have inserted the
    // null pointer into the map before we can initialize it, erase the
    // inserted null element.
    SCOPE_FAIL {
      lockedHistMap->erase(ret.first);
    };

    newHistogram = std::make_shared<SyncHistogram>(
        std::in_place, bucketWidth, min, max, **defaultStat_.rlock());
    ret.first->second = newHistogram;

    // End of scope:
    // Note that both our lock around histMap_ and our SCOPE_FAIL statement
    // expire here.
  }

  // Invoke the HistogramExporter after releasing the histMap_ lock.
  HistogramExporter::exportBuckets(newHistogram, name, dynamicStrings_);
  return true;
}

void ExportedHistogramMap::checkAdd(
    StringPiece name,
    const HistogramPtr& item,
    int64_t bucketWidth,
    int64_t min,
    int64_t max) const {
  // Log an error if someone tries to create an existing histogram with
  // different parameters.
  auto lockedHist = item->wlock();
  if (lockedHist->getBucketSize() != bucketWidth ||
      lockedHist->getMin() != min || lockedHist->getMax() != max) {
    LOG(ERROR) << "Attempted to create an existing histogram with "
               << "different parameters: " << name << ": old = ("
               << lockedHist->getBucketSize() << ", " << lockedHist->getMin()
               << ", " << lockedHist->getMax() << ") new = (" << bucketWidth
               << ", " << min << ", " << max << ")";
  }
}

bool ExportedHistogramMap::exportPercentile(StringPiece name, int percentile) {
  HistogramPtr item = getHistogramUnlocked(name);
  if (item == nullptr) {
    LOG(ERROR) << "Attempted to export non-existent histogram: " << name;
    return false;
  }

  HistogramExporter::exportPercentile(item, name, percentile, dynamicCounters_);
  return true;
}

void ExportedHistogramMap::unexportPercentile(
    StringPiece name,
    int percentile) {
  HistogramPtr item = getHistogramUnlocked(name);
  if (item == nullptr) {
    return;
  }

  HistogramExporter::unexportPercentile(
      item, name, percentile, dynamicCounters_);
}

bool ExportedHistogramMap::exportStat(StringPiece name, ExportType type) {
  HistogramPtr item = getHistogramUnlocked(name);
  if (item == nullptr) {
    LOG(ERROR) << "Attempted to export non-existent histogram: " << name;
    return false;
  }

  HistogramExporter::exportStat(item, name, type, dynamicCounters_);
  return true;
}

void ExportedHistogramMap::unexportStat(StringPiece name, ExportType type) {
  HistogramPtr item = getHistogramUnlocked(name);
  if (item == nullptr) {
    return;
  }

  HistogramExporter::unexportStat(item, name, type, dynamicCounters_);
}

void ExportedHistogramMap::clearAllHistograms() {
  auto lockedHistMap = histMap_.wlock();
  for (auto& histPtrKvp : *lockedHistMap) {
    histPtrKvp.second->wlock()->clear();
  }
}
} // namespace facebook::fb303
