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

#include <glog/logging.h>

namespace facebook {
namespace fb303 {

template <typename T>
void CallbackValuesMap<T>::getValues(ValuesMap* output) const {
  CHECK(output);

  // Do not perform actual callbacks under the global lock otherwise there is
  // a potential for deadlocks.  Make a copy of the map first.  See t660896
  // for more details.
  std::vector<std::pair<std::string, std::shared_ptr<CallbackEntry>>> mapCopy;
  size_t toReserve;
  {
    folly::SharedMutex::ReadHolder g(mutex_);
    toReserve = callbackMap_.size();
  }
  mapCopy.reserve(toReserve);
  {
    folly::SharedMutex::ReadHolder g(mutex_);
    for (const auto& entry : callbackMap_) {
      mapCopy.emplace_back(entry.first.str(), entry.second);
    }
  }

  for (auto& it : mapCopy) {
    T result;
    if (it.second->getValue(&result)) {
      // if the entry was unregistered underneath, getValue returns failure
      (*output)[std::move(it.first)] = std::move(result);
    }
  }
}

template <typename T>
bool CallbackValuesMap<T>::getValue(folly::StringPiece name, T* output) const {
  CHECK(output);
  std::shared_ptr<CallbackEntry> entry;
  {
    folly::SharedMutex::ReadHolder g(mutex_);
    auto it = callbackMap_.find(name);
    if (it == callbackMap_.end()) {
      return false;
    }
    entry = it->second;
    // do not perform actual callbacks under the global lock
    // otherwise there is a potential for deadlocks
    // see t660896 for more details
  }

  if (!entry->getValue(output)) {
    // if the entry was unregistered underneath, getValue returns failure
    return false;
  }

  return true;
}

template <typename T>
bool CallbackValuesMap<T>::contains(folly::StringPiece name) const {
  folly::SharedMutex::ReadHolder g(mutex_);
  return callbackMap_.find(name) != callbackMap_.end();
}

template <typename T>
void CallbackValuesMap<T>::getKeys(std::vector<std::string>* keys) const {
  folly::SharedMutex::ReadHolder g(mutex_);
  keys->reserve(keys->size() + callbackMap_.size());
  for (const auto& entry : callbackMap_) {
    keys->push_back(entry.first.str());
  }
}

template <typename T>
size_t CallbackValuesMap<T>::getNumKeys() const {
  folly::SharedMutex::ReadHolder g(mutex_);
  return callbackMap_.size();
}

template <typename T>
void CallbackValuesMap<T>::registerCallback(
    folly::StringPiece name,
    const Callback& cob) {
  folly::SharedMutex::WriteHolder g(mutex_);
  callbackMap_[name] = std::make_shared<CallbackEntry>(cob);
}

template <typename T>
bool CallbackValuesMap<T>::unregisterCallback(folly::StringPiece name) {
  folly::SharedMutex::WriteHolder g(mutex_);
  auto entry = callbackMap_.find(name);
  if (entry == callbackMap_.end()) {
    return false;
  }
  entry->second->clear();
  callbackMap_.erase(entry);
  VLOG(5) << "Unregistered  callback: " << name;
  return true;
}

template <typename T>
void CallbackValuesMap<T>::clear() {
  folly::SharedMutex::WriteHolder g(mutex_);
  for (auto& entry : callbackMap_) {
    entry.second->clear();
  }
  callbackMap_.clear();
}

template <typename T>
CallbackValuesMap<T>::CallbackEntry::CallbackEntry(const Callback& callback)
    : callback_(callback) {}

template <typename T>
void CallbackValuesMap<T>::CallbackEntry::clear() {
  folly::SharedMutex::WriteHolder lock(rwlock_);
  callback_ = Callback();
}

template <typename T>
bool CallbackValuesMap<T>::CallbackEntry::getValue(T* output) const {
  folly::SharedMutex::ReadHolder lock(rwlock_);
  if (!callback_) {
    return false;
  }
  *output = callback_();
  return true;
}

} // namespace fb303
} // namespace facebook
