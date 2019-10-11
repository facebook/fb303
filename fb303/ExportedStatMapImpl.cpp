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

#include <fb303/ExportedStatMapImpl.h>
#include <fb303/TimeseriesExporter.h>

using folly::StringPiece;
using std::shared_ptr;
using std::string;

namespace facebook {
namespace fb303 {

using fb303::TimeseriesExporter;

void ExportedStatMapImpl::exportStat(
    LockableStat stat,
    StringPiece name,
    ExportType exportType) {
  StatPtr item = stat.getStatPtr();
  TimeseriesExporter::exportStat(item, exportType, name, dynamicCounters_);
}

ExportedStatMapImpl::LockableStat ExportedStatMapImpl::getLockableStat(
    StringPiece name,
    const ExportType* type) {
  return ExportedStatMapImpl::LockableStat(
      ExportedStatMap::getStatPtr(name, type));
}

ExportedStatMapImpl::LockableStat ExportedStatMapImpl::getLockableStatNoExport(
    StringPiece name,
    bool* createdPtr,
    const ExportedStat* copyMe) {
  return ExportedStatMapImpl::LockableStat(
      ExportedStatMap::getStatPtrNoExport(name, createdPtr, copyMe));
}

} // namespace fb303
} // namespace facebook
