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

#include <fb303/ExportedStatMap.h>
#include <fb303/TimeseriesExporter.h>

using folly::StringPiece;
using std::shared_ptr;
using std::string;

namespace facebook {
namespace fb303 {

void ExportedStatMap::exportStat(
    StringPiece name,
    ExportType type,
    const ExportedStat* copyMe) {
  StatPtr item = getStatPtrNoExport(name, nullptr, copyMe);
  TimeseriesExporter::exportStat(item, type, name, dynamicCounters_);
}

ExportedStatMap::StatPtr ExportedStatMap::getStatPtr(
    StringPiece name,
    const ExportType* exportType) {
  // find the stat
  bool created = false;
  StatPtr item = getStatPtrNoExport(name, &created);

  if (created) {
    // if newly created, add export types
    if (nullptr != exportType) {
      TimeseriesExporter::exportStat(item, *exportType, name, dynamicCounters_);
    } else {
      for (auto type : defaultTypes_) {
        TimeseriesExporter::exportStat(item, type, name, dynamicCounters_);
      }
    }
  }
  return item;
}

ExportedStatMap::StatPtr ExportedStatMap::getStatPtrNoExport(
    StringPiece name,
    bool* createdPtr,
    const ExportedStat* copyMe) {
  if (createdPtr) {
    *createdPtr = false;
  }

  {
    auto rlock = statMap_.rlock();
    auto iter = rlock->find(name);
    if (iter != rlock->end()) {
      return iter->second;
    }
  }

  auto ulock = statMap_.ulock();
  auto iter = ulock->find(name);
  if (iter != ulock->end()) {
    // Stat was populated before we acquired the ulock.
    return iter->second;
  }

  auto value = std::make_shared<SyncStat>(defaultStat_);
  if (copyMe) {
    *value = *copyMe;
  }

  if (createdPtr) {
    *createdPtr = true;
  }

  auto wlock = ulock.moveFromUpgradeToWrite();
  auto item = wlock->try_emplace(name, std::move(value));
  DCHECK(item.second);
  return item.first->second;
}

void ExportedStatMap::unExportStatAll(StringPiece name) {
  // Get unlocked item as we will not access the value of the item
  // And the function called on the value assume that they can access
  // the value without locking
  auto lockedStatMap = statMap_.wlock();
  auto stat = lockedStatMap->find(name);
  if (stat != lockedStatMap->end()) {
    for (auto type : ExportTypeMeta::kExportTypes) {
      TimeseriesExporter::unExportStat(
          stat->second, type, name, dynamicCounters_);
    }
    lockedStatMap->erase(name);
  }
}

void ExportedStatMap::forgetAllStats() {
  statMap_.wlock()->clear();
}

void ExportedStatMap::forgetStatsFor(StringPiece name) {
  statMap_.wlock()->erase(name);
}

void ExportedStatMap::clearAllStats() {
  auto lockedStatMap = statMap_.wlock();
  for (auto& statPtrKvp : *lockedStatMap) {
    statPtrKvp.second->lock()->clear();
  }
}
} // namespace fb303
} // namespace facebook
