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

namespace facebook::fb303::detail {

template <size_t N>
DynamicQuantileStatWrapper<N>::DynamicQuantileStatWrapper(
    std::string keyFormat,
    folly::Range<const ExportType*> stats,
    folly::Range<const double*> quantiles,
    folly::Range<const size_t*> timeseriesLengths)
    : keyFormat_(std::move(keyFormat)) {
  spec_.stats.assign(stats.begin(), stats.end());
  spec_.quantiles.assign(quantiles.begin(), quantiles.end());
  spec_.timeseriesLengths.assign(
      timeseriesLengths.begin(), timeseriesLengths.end());
}

template <size_t N>
template <typename... Args>
void DynamicQuantileStatWrapper<N>::addValue(
    double value,
    std::chrono::steady_clock::time_point now,
    Args&&... subkeys) {
  getStatEntry(std::forward<Args>(subkeys)...).addValue(value, now);
}

template <size_t N>
template <typename... Args>
void DynamicQuantileStatWrapper<N>::addValue(double value, Args&&... subkeys) {
  addValue(
      value, std::chrono::steady_clock::now(), std::forward<Args>(subkeys)...);
}

template <size_t N>
template <typename... Args>
QuantileStat& DynamicQuantileStatWrapper<N>::getStatEntry(Args&&... subkeys) {
  auto& local = *localCache_;
  auto const keytup = internal::SubkeyUtils<N>::decaySubkeys(subkeys...);
  auto it = local.find(keytup);
  if (FOLLY_LIKELY(it != local.end())) {
    return *(*it)->stat;
  }
  return getStatEntrySlow(std::forward<Args>(subkeys)...);
}

template <size_t N>
template <typename... Args>
QuantileStat& DynamicQuantileStatWrapper<N>::getStatEntrySlow(
    Args&&... subkeys) {
  auto& local = *localCache_;
  const auto& entry =
      getOrCreateGlobal(internal::SubkeyUtils<N>::makeSubkeyArray(subkeys...));
  local.insert(&entry);
  return *entry.stat;
}

template <size_t N>
auto DynamicQuantileStatWrapper<N>::getOrCreateGlobal(SubkeyArray subkeyArray)
    -> const Entry& {
  {
    auto rlock = globalCache_.rlock();
    auto iter = rlock->find(subkeyArray);
    if (iter != rlock->end()) {
      return *iter;
    }
  }

  auto ulock = globalCache_.ulock();
  auto iter = ulock->find(subkeyArray);
  if (iter != ulock->end()) {
    return *iter;
  }

  auto formattedKey =
      internal::SubkeyUtils<N>::formatKey(keyFormat_, subkeyArray);
  auto stat = ServiceData::get()->getQuantileStat(
      formattedKey, spec_.stats, spec_.quantiles, spec_.timeseriesLengths);

  auto wlock = ulock.moveFromUpgradeToWrite();
  auto [it, inserted] =
      wlock->emplace(Entry{std::move(subkeyArray), std::move(stat)});
  return *it;
}

} // namespace facebook::fb303::detail
