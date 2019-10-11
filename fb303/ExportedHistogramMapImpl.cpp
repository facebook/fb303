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

#include <fb303/ExportedHistogramMapImpl.h>
#include <fb303/HistogramExporter.h>
#include <fb303/LegacyClock.h>

using folly::StringPiece;

namespace facebook {
namespace fb303 {

CounterType getHistogramPercentile(
    const ExportedHistogramMapImpl::LockableHistogram& hist,
    int level,
    double percentile) {
  auto guard = hist.makeLockGuard();

  // make sure the histogram is up to date and data is decayed appropriately
  hist.updateLocked(guard, get_legacy_stats_time());

  // return the estimated percentile value for the given percentile
  return hist.getPercentileEstimateLocked(guard, percentile, level);
}

ExportedHistogramMapImpl::HistogramPtr ExportedHistogramMapImpl::ensureExists(
    StringPiece name,
    bool crashIfMissing) {
  ExportedHistogramMapImpl::HistogramPtr item = getHistogramUnlocked(name);
  if (item == nullptr) {
    if (crashIfMissing) {
      LOG(FATAL) << "Accessing non-existent histogram: " << name;
    } else {
      return HistogramPtr();
    }
  }
  return item;
}

} // namespace fb303
} // namespace facebook
