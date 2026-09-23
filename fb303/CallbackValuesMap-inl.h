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

#include <algorithm>
#include <span>
#include <utility>

#include <fb303/detail/RegexUtil.h>
#include <folly/MapUtil.h>
#include <folly/algorithm/BinaryHeap.h>
#include <folly/container/Reserve.h>
#include <glog/logging.h>

namespace facebook {
namespace fb303 {

namespace detail {
// K-way merges per-shard name-sorted entry snapshots in global sorted order,
// invoking each callback and hint-inserting the result into output.
template <typename ValuesMap, typename EntryPtr>
void invokeAndMergeInto(
    ValuesMap* output,
    std::span<const std::vector<EntryPtr>> shards) {
  // Heap of remaining runs, one per non-empty shard.
  std::vector<std::span<const EntryPtr>> heap;
  heap.reserve(shards.size());
  for (const auto& entries : shards) {
    if (!entries.empty()) {
      heap.emplace_back(entries);
    }
  }
  // Reversed: make_heap builds a max-heap, and the merge wants the least name.
  auto cmp = [](const auto& a, const auto& b) {
    return a.front()->name() > b.front()->name();
  };
  std::make_heap(heap.begin(), heap.end(), cmp);

  auto hint = output->begin();
  while (!heap.empty()) {
    const auto& entry = heap.front().front();
    typename ValuesMap::mapped_type result;
    // if the entry was unregistered underneath, getValue returns false
    if (entry->getValue(&result)) {
      // Names ascend, so each insert hints the next and skips the tree descent.
      // Assigning, not emplacing, so these override same-named keys already in
      // output.
      hint = output->insert_or_assign(hint, entry->name(), std::move(result));
    }
    heap.front() = heap.front().subspan(1);
    if (heap.front().empty()) {
      std::swap(heap.front(), heap.back());
      heap.pop_back();
    }
    // Top changed, by advance or by swap-in. folly::down_heap re-sifts it in
    // one pass; std has only pop_heap plus push_heap, which sift twice.
    folly::down_heap(heap.begin(), heap.end(), cmp);
  }
}
} // namespace detail

template <typename T>
void CallbackValuesMap<T>::getValues(ValuesMap* output) const {
  CHECK(output);

  // If callbacks were to be invoked under the lock, that could deadlock
  // so copy under the shared lock and invoke after the lock is released.
  // Each shard is sorted while its entries are still hot, then all shards
  // are merged in key order for hinted insertion into the output map.
  std::vector<std::vector<std::shared_ptr<CallbackEntry>>> shards;
  shards.reserve(kNumShards);
  for (const auto& callbackMap : callbackMaps_) {
    std::vector<std::shared_ptr<CallbackEntry>> entries;
    entries.reserve(callbackMap.rlock()->map.size());
    callbackMap.withRLock([&](auto const& map) {
      // avoid vector::assign() since std::distance() would walk the set
      for (const auto& entry : map.map) {
        entries.push_back(entry);
      }
    });
    std::sort(entries.begin(), entries.end(), [](const auto& a, const auto& b) {
      return a->name() < b->name();
    });
    shards.push_back(std::move(entries));
  }

  detail::invokeAndMergeInto(output, std::span{std::as_const(shards)});
}

template <typename T>
bool CallbackValuesMap<T>::getValue(folly::StringPiece name, T* output) const {
  CHECK(output);

  // if callbacks were to be invoked under the lock, that could deadlock
  // so copy under the shared lock and invoke after the lock is released
  auto entry = getCallback(name);
  // if the entry was unregistered underneath, getValue returns false
  return entry && entry->getValue(output);
}

template <typename T>
bool CallbackValuesMap<T>::contains(folly::StringPiece name) const {
  return callbackMaps_[getShard(name)].rlock()->map.contains(name);
}

template <typename T>
void CallbackValuesMap<T>::getKeys(std::vector<std::string>* keys) const {
  for (const auto& callbackMap : callbackMaps_) {
    auto rlock = callbackMap.rlock();
    folly::grow_capacity_by(*keys, rlock->map.size());
    for (const auto& entry : rlock->map) {
      keys->emplace_back(entry->name());
    }
  }
}

template <typename T>
void CallbackValuesMap<T>::getRegexKeys(
    std::vector<std::string>& keys,
    const folly::RegexMatchCache::regex_key_and_view& regex,
    const folly::RegexMatchCache::time_point now) const {
  for (const auto& callbackMap : callbackMaps_) {
    detail::cachedFindMatchesSnapshot(keys, callbackMap, regex, now);
  }
}

template <typename T>
size_t CallbackValuesMap<T>::getNumKeys() const {
  size_t size = 0;
  for (const auto& callbackMap : callbackMaps_) {
    size += callbackMap.rlock()->map.size();
  }
  return size;
}

template <typename T>
void CallbackValuesMap<T>::registerCallback(
    folly::StringPiece name,
    Callback cob,
    bool overwrite) {
  auto& callbackMap = callbackMaps_[getShard(name)];
  if (!overwrite && callbackMap.rlock()->map.contains(name)) {
    return;
  }

  auto ulock = callbackMap.ulock();
  auto iter = ulock->map.find(name);
  if (!overwrite && iter != ulock->map.end()) {
    return;
  }
  auto entry = std::make_shared<CallbackEntry>(name.str(), std::move(cob));
  auto wlock = ulock.moveFromUpgradeToWrite();
  if (iter != wlock->map.end()) {
    // Cannot replace an entry in a set, we need to remove it first.
    detail::cachedEraseString(*wlock, iter);
  }
  auto inserted = detail::cachedAddString(*wlock, std::move(entry)).second;
  DCHECK(inserted);
}

template <typename T>
bool CallbackValuesMap<T>::unregisterCallback(folly::StringPiece name) {
  auto wlock = callbackMaps_[getShard(name)].wlock();
  auto iter = wlock->map.find(name);
  if (iter == wlock->map.end()) {
    return false;
  }
  auto callback = *iter;
  detail::cachedEraseString(*wlock, iter);
  VLOG(5) << "Unregistered callback: " << name;

  // clear the callback after releasing the lock
  wlock.unlock();
  callback->clear();
  return true;
}

template <typename T>
void CallbackValuesMap<T>::clear() {
  for (auto& callbackMap : callbackMaps_) {
    auto wlock = callbackMap.wlock();
    for (auto& entry : wlock->map) {
      entry->clear();
    }
    detail::cachedClearStrings(*wlock);
  }
}

template <typename T>
void CallbackValuesMap<T>::trimRegexCache(
    const folly::RegexMatchCache::time_point expiry) {
  for (auto& callbackMap : callbackMaps_) {
    detail::cachedTrimStale(callbackMap, expiry);
  }
}

template <typename T>
std::shared_ptr<typename CallbackValuesMap<T>::CallbackEntry>
CallbackValuesMap<T>::getCallback(folly::StringPiece name) const {
  auto map = callbackMaps_[getShard(name)].rlock();
  auto iter = map->map.find(name);
  return iter != map->map.end() ? *iter : nullptr;
}

template <typename T>
CallbackValuesMap<T>::CallbackEntry::CallbackEntry(
    std::string&& name,
    Callback&& callback)
    : name_(std::move(name)), callback_(std::move(callback)) {}

template <typename T>
void CallbackValuesMap<T>::CallbackEntry::clear() {
  *callback_.wlock() = Callback();
}

template <typename T>
bool CallbackValuesMap<T>::CallbackEntry::getValue(T* output) const {
  auto rlock = callback_.rlock();
  if (!*rlock) {
    return false;
  }
  *output = (*rlock)();
  return true;
}

} // namespace fb303
} // namespace facebook
