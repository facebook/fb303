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

#include <fb303/detail/RegexUtil.h>
#include <folly/MapUtil.h>
#include <glog/logging.h>

namespace facebook {
namespace fb303 {

template <typename T>
void CallbackValuesMap<T>::getValues(ValuesMap* output) const {
  CHECK(output);

  // if callbacks were to be invoked under the lock, that could deadlock
  // so copy under the shared lock and invoke after the lock is released
  std::vector<std::pair<std::string, std::shared_ptr<CallbackEntry>>> mapCopy;
  callbackMap_.withRLock([&](auto const& map) {
    // a vector to avoid N allocations when copying a std::map with N entries
    mapCopy.reserve(map.map.size());
    for (const auto& entry : map.map) {
      mapCopy.emplace_back(entry.first.str(), entry.second);
    }
  });

  for (auto& it : mapCopy) {
    T result;
    // if the entry was unregistered underneath, getValue returns false
    if (it.second->getValue(&result)) {
      (*output)[std::move(it.first)] = std::move(result);
    }
  }
}

template <typename T>
bool CallbackValuesMap<T>::getValue(folly::StringPiece name, T* output) const {
  CHECK(output);

  // if callbacks were to be invoked under the lock, that could deadlock
  // so copy under the shared lock and invoke after the lock is released
  auto entry = folly::get_default(callbackMap_.rlock()->map, name);

  // if the entry was unregistered underneath, getValue returns false
  return entry && entry->getValue(output);
}

template <typename T>
bool CallbackValuesMap<T>::contains(folly::StringPiece name) const {
  return nullptr != folly::get_ptr(callbackMap_.rlock()->map, name);
}

template <typename T>
void CallbackValuesMap<T>::getKeys(std::vector<std::string>* keys) const {
  auto rlock = callbackMap_.rlock();
  keys->reserve(keys->size() + rlock->map.size());
  for (const auto& [key, _] : rlock->map) {
    keys->emplace_back(key);
  }
}

template <typename T>
void CallbackValuesMap<T>::getRegexKeys(
    std::vector<std::string>& keys,
    const std::string& regex) const {
  detail::getRegexKeysImpl(keys, regex, callbackMap_);
}

template <typename T>
size_t CallbackValuesMap<T>::getNumKeys() const {
  return callbackMap_.rlock()->map.size();
}

template <typename T>
void CallbackValuesMap<T>::registerCallback(
    folly::StringPiece name,
    const Callback& cob) {
  auto wlock = callbackMap_.wlock();
  wlock->map[name] = std::make_shared<CallbackEntry>(cob);

  // avoid fetch_add() to avoid extra fences, since we hold the lock already
  uint64_t epoch = wlock->mapEpoch.load();
  wlock->mapEpoch.store(epoch + 1);
}

template <typename T>
bool CallbackValuesMap<T>::unregisterCallback(folly::StringPiece name) {
  auto wlock = callbackMap_.wlock();
  auto entry = wlock->map.find(name);
  if (entry == wlock->map.end()) {
    return false;
  }
  entry->second->clear();

  // avoid fetch_add() to avoid extra fences, since we hold the lock already
  uint64_t epoch = wlock->mapEpoch.load();
  wlock->mapEpoch.store(epoch + 1);

  wlock->map.erase(entry);
  VLOG(5) << "Unregistered  callback: " << name;
  return true;
}

template <typename T>
void CallbackValuesMap<T>::clear() {
  auto wlock = callbackMap_.wlock();
  for (auto& entry : wlock->map) {
    entry.second->clear();
  }
  // avoid fetch_add() to avoid extra fences, since we hold the lock already
  uint64_t epoch = wlock->mapEpoch.load();
  wlock->mapEpoch.store(epoch + 1);
  wlock->map.clear();
}

template <typename T>
std::shared_ptr<typename CallbackValuesMap<T>::CallbackEntry>
CallbackValuesMap<T>::getCallback(folly::StringPiece name) {
  return folly::get_default(callbackMap_.rlock()->map, name);
}

template <typename T>
CallbackValuesMap<T>::CallbackEntry::CallbackEntry(const Callback& callback)
    : callback_(callback) {}

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
