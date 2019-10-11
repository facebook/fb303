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

#pragma once

#include <fb303/SynchMap.h>
#include <glog/logging.h>

namespace facebook {
namespace fb303 {

template <class K, class V, class T>
SynchMap<K, V, T>::SynchMap() {}

template <class K, class V, class T>
SynchMap<K, V, T>::~SynchMap() {}

template <class K, class V, class T>
bool SynchMap<K, V, T>::contains(LookupType key) const {
  folly::SharedMutex::ReadHolder g(lock_);
  return map_.find(key) != map_.end();
}

template <class K, class V, class T>
typename SynchMap<K, V, T>::LockedValuePtr SynchMap<K, V, T>::get(
    LookupType key) {
  folly::SharedMutex::ReadHolder g(lock_);
  typename MapType::iterator it = map_.find(key);
  if (it == map_.end()) {
    return createLockedValuePtr(nullptr, &g);
  }

  LockAndItem* value = &(it->second);
  CHECK(value->first);
  CHECK(value->second);
  return createLockedValuePtr(value, &g);
}

template <class K, class V, class T>
typename SynchMap<K, V, T>::LockedValuePtr SynchMap<K, V, T>::getOrCreate(
    LookupType key,
    const ValueType& defaultVal,
    bool* createdPtr) {
  return emplace(key, createdPtr, defaultVal);
}

template <class K, class V, class T>
template <typename... Args>
typename SynchMap<K, V, T>::LockedValuePtr
SynchMap<K, V, T>::emplace(LookupType key, bool* createdPtr, Args&&... args) {
  if (createdPtr) {
    *createdPtr = false;
  }

  // Attempt #1: see if it's already there
  {
    folly::SharedMutex::ReadHolder g(lock_);
    typename MapType::iterator it = map_.find(key);
    if (it != map_.end()) {
      LockAndItem* value = &it->second;
      CHECK(value->first && value->second);
      return createLockedValuePtr(value, &g);
    }
  }

  // Can't find the thing, create a new value and insert it if not there.
  // Allocate outside the lock guard.
  auto mapped = std::make_pair(
      std::make_shared<std::mutex>(),
      std::make_shared<V>(std::forward<Args>(args)...));

  folly::SharedMutex::WriteHolder g(lock_);
  auto insertResult = map_.emplace(key, std::move(mapped));
  LockAndItem* value = &insertResult.first->second;
  if (!insertResult.second) {
    // Someone just inserted it. Oh well.
    CHECK(value->first && value->second);
    folly::SharedMutex::ReadHolder h(std::move(g));
    return createLockedValuePtr(value, &h);
  }

  CHECK(value->first && value->second);

  if (createdPtr) {
    *createdPtr = true;
  }

  folly::SharedMutex::ReadHolder h(std::move(g));
  return createLockedValuePtr(value, &h);
}

template <class K, class V, class T>
typename SynchMap<K, V, T>::LockAndItem SynchMap<K, V, T>::getUnlocked(
    LookupType key) {
  folly::SharedMutex::ReadHolder g(lock_);
  typename MapType::iterator it = map_.find(key);
  if (it == map_.end()) {
    return LockAndItem();
  }

  LockAndItem value = it->second;
  CHECK(value.first);
  CHECK(value.second);
  return value;
}

template <class K, class V, class T>
typename SynchMap<K, V, T>::LockAndItem SynchMap<K, V, T>::getOrCreateUnlocked(
    LookupType key,
    const ValueType& defaultVal,
    bool* createdPtr) {
  return emplaceUnlocked(key, createdPtr, defaultVal);
}

template <class K, class V, class T>
template <typename... Args>
typename SynchMap<K, V, T>::LockAndItem SynchMap<K, V, T>::emplaceUnlocked(
    LookupType key,
    bool* createdPtr,
    Args&&... args) {
  !createdPtr || (*createdPtr = false);

  // Attempt #1: see if it's already there
  {
    folly::SharedMutex::ReadHolder g(lock_);
    typename MapType::iterator it = map_.find(key);
    if (it != map_.end()) {
      return it->second;
    }
  }

  // Can't find the thing, create a new value
  typename MapType::value_type toInsert = {key, {nullptr, nullptr}};
  toInsert.second.first.reset(new std::mutex());
  toInsert.second.second.reset(new V(std::forward<Args>(args)...));

  // Attempt #2: this time, insert the new value if not there
  folly::SharedMutex::WriteHolder g(lock_);
  auto insertResult = map_.insert(toInsert);
  if (!insertResult.second) {
    // Someone just inserted it. Oh well. Nutt'n to do.
  } else {
    !createdPtr || (*createdPtr = true);
  }

  return insertResult.first->second;
}

template <class K, class V, class T>
template <class Fn>
void SynchMap<K, V, T>::forEach(const Fn& fn) {
  folly::SharedMutex::ReadHolder g(lock_);
  for (const auto& elem : map_) {
    const LockAndItem* value = &elem.second;
    CHECK(value->first && value->second);

    std::lock_guard<std::mutex> itemGuard(value->first.get());
    fn(elem.first, *value->second.get());
  }
}

template <class K, class V, class T>
void SynchMap<K, V, T>::set(LookupType key, const ValueType& val) {
  bool created;
  LockedValuePtr value = getOrCreate(key, val, &created);
  if (!created) {
    // item already existing, need to set its value to the right thing
    // No need to lock, it's already locked
    *value = val;
  }
}

template <class K, class V, class T>
typename SynchMap<K, V, T>::UniqueValuePtr
SynchMap<K, V, T>::createUniqueValuePtr(
    LockAndItem* value,
    folly::SharedMutex::ReadHolder* g) {
  if (!value || !value->first || !value->second) {
    return UniqueValuePtr();
  }

  // Carefully controlled sequence, do NOT change: the releaser of the
  // result keeps the reference count floating even if the entry is
  // deleted immediately after the release.
  UniqueValuePtr result(
      value->second.get(), MutexReleaser(value->first, value->second));
  // Create a copy of value->first().get() because value may be
  // deallocated by another thread right after g->release().
  auto const itemLockPtrCopy = value->first.get();
  if (g)
    g->unlock();
  // Acquire the value's lock and return a special unique_ptr<> that
  // will release the lock when it is destroyed.
  itemLockPtrCopy->lock();
  // Ready to rock the casbah
  return result;
}

template <class K, class V, class T>
bool SynchMap<K, V, T>::erase(LookupType key) {
  folly::SharedMutex::WriteHolder g(lock_);
  return map_.erase(key) > 0;
}

template <class K, class V, class T>
void SynchMap<K, V, T>::clear() {
  folly::SharedMutex::WriteHolder g(lock_);
  map_.clear();
}

} // namespace fb303
} // namespace facebook
