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
#include <folly/MapUtil.h>

namespace facebook::fb303::detail {

template <size_t N>
DynamicQuantileStatWrapper<N>::DynamicQuantileStatWrapper(
    std::string keyFormat,
    folly::Range<const ExportType*> stats,
    folly::Range<const double*> quantiles,
    folly::Range<const size_t*> timeseriesLengths)
    : key_(std::move(keyFormat), nullptr) {
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
  auto key = key_.getFormattedKeyWithExtra(std::forward<Args>(subkeys)...);
  if (key.second.get() == nullptr) {
    auto& cache = *cache_;
    auto ptr = folly::get_ptr(cache, key.first);
    if (!ptr) {
      key.second.get() = cache[key.first] = ServiceData::get()->getQuantileStat(
          key.first, spec_.stats, spec_.quantiles, spec_.timeseriesLengths);
    }
  }
  key.second.get()->addValue(value, now);
}

template <size_t N>
template <typename... Args>
void DynamicQuantileStatWrapper<N>::addValue(double value, Args&&... subkeys) {
  addValue(
      value, std::chrono::steady_clock::now(), std::forward<Args>(subkeys)...);
}

} // namespace facebook::fb303::detail
