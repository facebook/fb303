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
#include <functional>
#include <string>
#include <vector>

#include <folly/Chrono.h>
#include <folly/MapUtil.h>
#include <folly/container/F14Map.h>
#include <folly/container/RegexMatchCache.h>
#include <folly/container/Reserve.h>

namespace facebook::fb303::detail {

template <typename Map, typename Iter>
Iter cachedAddStringAfterInsert(Map& map, std::pair<Iter, bool> const& result) {
  auto const& iter = result.first;
  if (result.second) {
    auto rollback = folly::makeGuard([&] {
      if (!map.matches.hasString(&iter->first)) {
        map.map.erase(iter);
      }
    });
    map.matches.addString(&iter->first);
    rollback.dismiss();
  }
  return iter;
}

template <typename Map, typename... Arg>
auto cachedAddString(Map& map, folly::StringPiece const name, Arg&&... arg) {
  return cachedAddStringAfterInsert(
      map, map.map.emplace(name, std::forward<Arg>(arg)...));
}

template <typename Map, typename Iter>
void cachedEraseString(Map& map, Iter const& iter) {
  map.matches.eraseString(&iter->first);
  map.map.erase(iter);
}

template <typename Map>
void cachedClearStrings(Map& map) {
  map.matches.clear();
  map.map.clear();
}

void cachedFindMatchesCopyUnderSharedLock(
    std::vector<std::string>& out,
    folly::RegexMatchCache const& cache,
    folly::RegexMatchCacheKeyAndView const& regex,
    folly::RegexMatchCache::time_point now);

template <typename SyncMap>
void cachedFindMatches(
    std::vector<std::string>& out,
    SyncMap& map,
    folly::RegexMatchCacheKeyAndView const& regex,
    folly::RegexMatchCache::time_point const now) {
  auto r = map.rlock();
  if (!r->matches.isReadyToFindMatches(regex)) {
    r = {};
    auto w = map.wlock();
    const_cast<folly::RegexMatchCache&>(w->matches).prepareToFindMatches(regex);
    r = w.moveFromWriteToRead(); // atomic transition is required here
  }
  cachedFindMatchesCopyUnderSharedLock(out, r->matches, regex, now);
}

template <typename SyncMap>
void cachedTrimStale(
    SyncMap& map,
    folly::RegexMatchCache::time_point const expiry) {
  if (auto ulock = map.ulock(); ulock->matches.hasItemsToPurge(expiry)) {
    ulock.moveFromUpgradeToWrite()->matches.purge(expiry);
  }
}

} // namespace facebook::fb303::detail
