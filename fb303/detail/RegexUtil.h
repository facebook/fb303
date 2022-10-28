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

#include <chrono>
#include <string>
#include <vector>

#include <folly/Chrono.h>
#include <folly/MapUtil.h>
#include <folly/experimental/StringKeyedMap.h>

namespace facebook::fb303::detail {

constexpr folly::chrono::coarse_system_clock::time_point kTimeZero{
    std::chrono::seconds(0)};
constexpr folly::chrono::coarse_system_clock::duration kOneDay =
    std::chrono::hours(24);

// This gets the keys matching regex pattern
void filterRegexKeys(
    std::vector<std::string>& keysShared,
    const std::string& regex);

// adds pattern and matching keys to regex cache
void cacheRegexKeys(
    std::vector<std::string>& keys, // keys will be invalidated
    const std::string& regex,
    folly::StringKeyedMap<std::vector<std::string>>& regexCache);

// Generate list of extant keys that match the regex.
// SyncMap is folly::Synchronized of struct containing map, regexCache, epochs
template <class SyncMap>
void getRegexKeysImpl(
    std::vector<std::string>& outKeys,
    const std::string& regex,
    SyncMap& syncSource) {
  auto now = folly::chrono::coarse_system_clock::now();
  std::vector<std::string> wantKeys;
  uint64_t origMapEpoch = 0;
  bool forceClear = false;

  // First try fast path: cache hit under read lock
  {
    auto source = syncSource.rlock();
    origMapEpoch = source->mapEpoch.load();
    if ((source->cacheClearTime != kTimeZero) &&
        ((now - source->cacheClearTime) >= kOneDay)) {
      forceClear = true; // rare and expensive, like sapphires
    } else {
      if (origMapEpoch == source->cacheEpoch.load()) {
        auto cacheKeys = folly::get_ptr(source->regexCache, regex);
        if (cacheKeys) {
          // make a copy so we can let other threads have access
          outKeys.insert(outKeys.end(), cacheKeys->begin(), cacheKeys->end());
          return;
        }
      }

      // No cache hit, need to grab the list of keys while we're locked
      wantKeys.reserve(source->map.size());
      for (const auto& [key, _] : source->map) {
        wantKeys.emplace_back(key);
      }
    }
  }

  // Daily, do an expensive fully-locked cache refresh
  if (forceClear) {
    auto source = syncSource.wlock();
    wantKeys.reserve(source->map.size());
    for (const auto& [key, _] : source->map) {
      wantKeys.emplace_back(key);
    }
    filterRegexKeys(wantKeys, regex);
    outKeys.insert(outKeys.end(), wantKeys.begin(), wantKeys.end());
    source->regexCache.clear();
    source->cacheEpoch.store(source->mapEpoch.load());
    source->cacheClearTime = now;
    cacheRegexKeys(wantKeys, regex, source->regexCache);
    return;
  }

  // Here we have dropped the lock and will do some time-consuming stuff
  filterRegexKeys(wantKeys, regex);
  outKeys.insert(outKeys.end(), wantKeys.begin(), wantKeys.end());

  // At this point, we have outKeys we can return to the caller.  It may
  // be a few milliseconds out of date, but that's OK for one response.
  // We will now try to cache the response, but only if it's fresh.
  // It's OK to leave the cache invalid; future calls can repopulate it.
  // If mapEpoch doesn't match origMapEpoch, we can't cache what we have.
  // We use double-checked locking so we can bail out efficiently.

  // Unlocked, we can't know they're equal, but we can act as if they differ
  if (origMapEpoch != syncSource.unsafeGetUnlocked().mapEpoch.load()) {
    return;
  }

  // Now there's a good chance that the response is fresh.  We'll grab
  // a write lock before making sure it's still fresh.  If it's not
  // fresh, we just fall back to doing nothing, as above.
  {
    auto source = syncSource.wlock();
    if (origMapEpoch == source->mapEpoch.load()) {
      if (origMapEpoch != source->cacheEpoch) { // dirty cache: clear first
        source->regexCache.clear();
        source->cacheEpoch.store(origMapEpoch);
        source->cacheClearTime = now;
      }
      cacheRegexKeys(wantKeys, regex, source->regexCache);
    }
  }
}

} // namespace facebook::fb303::detail
