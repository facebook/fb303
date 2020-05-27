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

#include <folly/SharedMutex.h>
#include <folly/experimental/StringKeyedMap.h>

namespace facebook {
namespace fb303 {

template <typename T>
class CallbackValuesMap {
 public:
  CallbackValuesMap() = default;
  CallbackValuesMap(const CallbackValuesMap&) = delete;
  CallbackValuesMap& operator=(const CallbackValuesMap&) = delete;

  // output type to return all values
  typedef std::map<std::string, T> ValuesMap;
  typedef std::function<T()> Callback; // callback type

  /** Returns all the values in the map by invoking all the callbacks */
  void getValues(ValuesMap* output) const;

  /**
   * If the name is present, invokes the callback and places the result
   * in 'output' and returns true; returns false otherwise.
   */
  bool getValue(folly::StringPiece name, T* output) const;

  /** Returns true if the name is present in the map. */
  bool contains(folly::StringPiece name) const;

  /** Returns all keys present in the map */
  void getKeys(std::vector<std::string>* keys) const;

  /** Returns the number of keys present in the map */
  size_t getNumKeys() const;

  /**
   * Registers a given callback as associated with the given name.  Note
   * that a copy of the given cob is made, and that any previous registered
   * cob (if any) is replaced.
   */
  void registerCallback(folly::StringPiece name, const Callback& cob);

  /**
   * Unregisters the callback asssociated with the given name.
   *
   * @param name the name of the calback to unreg.
   * @return true if the callback was found and unregistered, false if
   * the callback with the given name wasn't found.
   */
  bool unregisterCallback(folly::StringPiece name);

  /**
   * Unregisters all callbacks.
   */
  void clear();

 private:
  class CallbackEntry {
   public:
    explicit CallbackEntry(const Callback& callback);
    void clear();
    bool getValue(T* output) const;

   private:
    Callback callback_;
    mutable folly::SharedMutex rwlock_;
  };

  folly::SharedMutex mutex_;
  typedef folly::StringKeyedMap<std::shared_ptr<CallbackEntry>> CallbackMap;
  CallbackMap callbackMap_;
};

} // namespace fb303
} // namespace facebook

#include <fb303/CallbackValuesMap-inl.h>
