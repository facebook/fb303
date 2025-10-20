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

/// Gets the key-accessor from the map. If the map has a possibly-static data-
/// member named fb303_key_accessor, returns that. Otherwise, returns a fallback
/// key-accessor which simply picks the first of a pair, which is suitable for
/// map value-types.
template <typename Map>
constexpr auto cachedGetKeyAccessor(Map const& map) noexcept {
  if constexpr (requires { map.fb303_key_accessor; }) {
    return map.fb303_key_accessor;
  } else {
    return [](auto const& _) -> auto const& { return _.first; };
  }
}

/// Gets the string-key pointer from the value. If the map has a possibly-static
/// data-member named fb303_key_accessor, uses that. Otherwise, falls back to
/// picking the first of a pair, which is suitable for map value-types.
template <typename Map, typename Value>
std::string const* cachedGetKeyPtr(
    Map const& map,
    Value const& value) noexcept {
  return &cachedGetKeyAccessor(map)(value); // does map have fb303_key_accessor?
}

/// Helper for cachedAddString.
template <typename Map, typename Iter>
std::pair<Iter, bool> cachedAddStringAfterInsert(
    Map& map,
    std::pair<Iter, bool> insertResult) {
  const auto& iter = insertResult.first;
  const auto* str = cachedGetKeyPtr(map, *iter);
  if (insertResult.second) {
    auto rollback = folly::makeGuard([&] {
      if (!map.matches.hasString(str)) {
        map.map.erase(iter);
      }
    });
    map.matches.addString(str);
    rollback.dismiss();
  }
  return insertResult;
}

/// Inserts into both the counter-map and the regex-match-cache. Handles
/// exceptions safely. Returns an (iterator, inserted) pair to the value in the
/// counter-map.
///
/// The arg... pack is the value getting inserted, as-if with this code:
///   emplace(std::forward<Arg>(arg)...)
///
/// Typically, the first item of the pack will be the key in some form, while
/// the remaining arguments are used to form the mapped object. But when the
/// counter-map is a set, all of the arguments are used to form the value.
///
/// Inverse of cachedEraseString.
template <typename Map, typename... Arg>
auto cachedAddString(Map& map, Arg&&... arg) {
  return cachedAddStringAfterInsert(
      map, map.map.emplace(std::forward<Arg>(arg)...));
}

/// Erases from both the counter-map and the regex-match-cache.
///
/// Inverse of cachedAddString.
template <typename Map, typename Iter>
void cachedEraseString(Map& map, Iter const& iter) {
  map.matches.eraseString(cachedGetKeyPtr(map, *iter));
  map.map.erase(iter);
}

/// Clears both the counter-map and the regex-match-cache.
///
/// Inverse of all the cachedAddString calls.
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
