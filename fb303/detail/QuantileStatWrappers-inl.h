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

#include <fb303/ServiceData.h>
#include <folly/synchronization/Hazptr.h>

namespace facebook::fb303::detail {

template <size_t N>
class DynamicQuantileStatWrapper<N>::MapHolder
    : public folly::hazptr_obj_base<DynamicQuantileStatWrapper<N>::MapHolder> {
 public:
  folly::F14NodeMap<
      DynamicQuantileStatWrapper<N>::SubkeyArray,
      std::shared_ptr<QuantileStat>,
      DynamicQuantileStatWrapper<N>::SubkeyArrayHash>
      map;
};

template <size_t N>
DynamicQuantileStatWrapper<N>::DynamicQuantileStatWrapper(
    std::string keyFormat,
    folly::Range<const ExportType*> stats,
    folly::Range<const double*> quantiles,
    folly::Range<const size_t*> timeseriesLengths)
    : format_(std::move(keyFormat)), stats_(new MapHolder()) {
  spec_.stats.insert(spec_.stats.end(), stats.begin(), stats.end());
  spec_.quantiles.insert(
      spec_.quantiles.end(), quantiles.begin(), quantiles.end());
  spec_.timeseriesLengths.insert(
      spec_.timeseriesLengths.end(),
      timeseriesLengths.begin(),
      timeseriesLengths.end());
}

template <size_t N>
template <typename... Args>
void DynamicQuantileStatWrapper<N>::addValue(
    double value,
    std::chrono::steady_clock::time_point now,
    Args&&... subkeys) {
  const SubkeyArray subkeyArray{{std::forward<Args>(subkeys)...}};

  folly::hazptr_holder<> h = folly::make_hazard_pointer<>();
  auto mapPtr = h.protect(stats_);

  auto it = mapPtr->map.find(subkeyArray);
  if (it != mapPtr->map.end()) {
    it->second->addValue(value, now);
    return;
  }

  auto key = folly::svformat(format_, SubkeyArray(subkeyArray));
  auto stat = ServiceData::get()->getQuantileStat(
      key, spec_.stats, spec_.quantiles, spec_.timeseriesLengths);

  MapHolder* newMap = nullptr;
  do {
    if (newMap) {
      delete newMap;
    }
    mapPtr = h.protect(stats_);
    newMap = new MapHolder(*mapPtr);
    newMap->map[subkeyArray] = stat;
  } while (
      !stats_.compare_exchange_weak(mapPtr, newMap, std::memory_order_acq_rel));
  h.reset_protection();
  mapPtr->retire();
  stat->addValue(value, now);
}

template <size_t N>
template <typename... Args>
void DynamicQuantileStatWrapper<N>::addValue(double value, Args&&... subkeys) {
  addValue(
      value, std::chrono::steady_clock::now(), std::forward<Args>(subkeys)...);
}

} // namespace facebook::fb303::detail
