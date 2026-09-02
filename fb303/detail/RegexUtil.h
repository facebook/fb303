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
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
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

/// Like cachedFindMatches, but runs the cold build without holding the
/// exclusive lock. The cold build is the O(queued-strings) boost::regex_match
/// scan over strings not yet coalesced for this regex; on the first build for a
/// regex, every string is queued. prepareToFindMatches holds the exclusive lock
/// for the scan's duration, which for a large universe stalls every concurrent
/// reader and writer for seconds. A single off-lock pass handles the common
/// case; strings added during the scan, or a purge that evicted the in-flight
/// regex, are drained synchronously under the lock.
template <typename SyncMap>
void cachedFindMatchesSnapshot(
    std::vector<std::string>& out,
    SyncMap& map,
    folly::RegexMatchCacheKeyAndView const& regex,
    folly::RegexMatchCache::time_point const now) {
  using EntryPtr =
      typename std::decay_t<decltype(map.rlock()->map)>::value_type;

  bool empty = false;
  {
    auto r = map.rlock();
    if (r->matches.isReadyToFindMatches(regex)) {
      cachedFindMatchesCopyUnderSharedLock(out, r->matches, regex, now);
      return;
    }
    empty = r->map.empty();
  }

  // Nothing to scan: the under-lock path registers the regex without compiling
  // it, so an empty map costs no regex construction. The emptiness read is not
  // carried into the delegated call, so a key arriving in that window is
  // scanned under the lock -- one shard's hold, which the under-lock path took
  // unconditionally. Correctness rests on map and the cache's string universe
  // staying in lockstep, which cachedAddString/cachedEraseString maintain for
  // every SyncMap instantiation here; nothing enforces it structurally.
  if (empty) {
    cachedFindMatches(out, map, regex, now);
    return;
  }

  // Compile before touching the cache: an invalid pattern throws here, leaving
  // the cache intact (prepareToFindMatches instead purges every cached regex).
  auto matcher = folly::RegexMatchCache::compile(regex.view);

  // Under the lock, pin every map entry alive before registering the regex, so
  // queued string pointers cannot dangle if a callback is unregistered during
  // the unlocked scan. Pinning before init keeps an allocation failure here
  // from orphaning a queued regex. Registering stamps accessed_at, which
  // protects the build from a purge at a stale expiry; a purge at the current
  // time still evicts it, and the drain below rebuilds under the lock.
  //
  // alive must stay held through finiPrepareToFindMatches, not merely through
  // the scan: publication identifies strings by address, so an entry released
  // early could be freed and a new one allocated at the same address, which
  // would publish an unrelated counter as a match.
  std::vector<EntryPtr> alive;
  auto handleOpt = std::invoke(
      [&]() -> std::optional<folly::RegexMatchCache::PrepareHandle> {
        auto w = map.wlock();
        if (w->matches.isReadyToFindMatches(regex)) {
          auto r = w.moveFromWriteToRead();
          cachedFindMatchesCopyUnderSharedLock(out, r->matches, regex, now);
          return std::nullopt;
        }
        alive.assign(w->map.begin(), w->map.end());
        auto h =
            w->matches.initPrepareToFindMatches(regex, std::move(matcher), now);
        return h;
      });
  if (!handleOpt) {
    return;
  }

  // The expensive scan, with no lock held. On throw the regex is registered but
  // never becomes ready, so erase it under the lock: that keeps the failure
  // scoped to this one regex, where the under-lock drain below would instead
  // purge every cached regex.
  auto result = std::invoke([&] {
    try {
      return std::move(*handleOpt).evaluate();
    } catch (...) {
      auto w = map.wlock();
      if (!w->matches.isReadyToFindMatches(regex)) {
        w->matches.eraseRegex(regex);
      }
      throw;
    }
  });

  // Two cases reach the drain: strings registered during the scan, bounded by
  // that window; or a purge that evicted the in-flight regex, which re-enters
  // prepareToFindMatches for a full shard scan -- the hold this avoids.
  {
    auto w = map.wlock();
    w->matches.finiPrepareToFindMatches(std::move(result));
    if (!w->matches.isReadyToFindMatches(regex)) {
      w->matches.prepareToFindMatches(regex);
    }
    auto r = w.moveFromWriteToRead();
    cachedFindMatchesCopyUnderSharedLock(out, r->matches, regex, now);
  }
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
