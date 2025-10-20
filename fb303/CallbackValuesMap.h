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

#include <functional>
#include <map>
#include <string>

#include <folly/Chrono.h>
#include <folly/Range.h>
#include <folly/Synchronized.h>
#include <folly/container/F14Set.h>
#include <folly/container/RegexMatchCache.h>
#include <folly/functional/Invoke.h>
#include <folly/synchronization/RelaxedAtomic.h>

namespace facebook {
namespace fb303 {

template <typename T>
class CallbackValuesMap {
 public:
  CallbackValuesMap() = default;
  CallbackValuesMap(const CallbackValuesMap&) = delete;
  CallbackValuesMap& operator=(const CallbackValuesMap&) = delete;

  // output type to return all values
  using ValuesMap = std::map<std::string, T>;
  using Callback = std::function<T()>; // callback type

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

  /* Returns the keys in the map that matches regex pattern */
  void getRegexKeys(std::vector<std::string>& keys, const std::string& regex)
      const {
    const auto key = folly::RegexMatchCache::regex_key_and_view(regex);
    const auto now = folly::RegexMatchCache::clock::now();
    getRegexKeys(keys, key, now);
  }
  void getRegexKeys(
      std::vector<std::string>& keys,
      const folly::RegexMatchCache::regex_key_and_view& regex,
      const folly::RegexMatchCache::time_point now) const;

  /** Returns the number of keys present in the map */
  size_t getNumKeys() const;

  /**
   * Registers a given callback as associated with the given name.  Note that a
   * copy of the given cob is made. If the name is already present in the map,
   * the passed callback replaces the previous one if overwrite = true,
   * otherwise the old callback is left in place.
   */
  void registerCallback(
      folly::StringPiece name,
      Callback cob,
      bool overwrite = true);

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

  void trimRegexCache(folly::RegexMatchCache::time_point expiry);

  class CallbackEntry {
   public:
    CallbackEntry(std::string&& name, Callback&& callback);

    void clear();
    bool getValue(T* output) const;
    const std::string& name() const {
      return name_;
    }

   private:
    const std::string name_;
    folly::Synchronized<Callback> callback_;
  };

  /**
   * Gets the callback for a given name.
   *
   * @param name the name of the callback
   * @return the callback associated with the given name, which can then be used
   * directly to retrieve callback values
   */
  std::shared_ptr<CallbackEntry> getCallback(folly::StringPiece name) const;

 private:
  // Combining counters map with cache and epoch numbers.  If epochs
  // match, cache is valid.
  struct CallbackMap {
    using SPtr = std::shared_ptr<CallbackEntry>;

    // Helpers for hash and key-equal functions
    struct cast_to_key_fn {
      constexpr folly::StringPiece operator()(
          folly::StringPiece name) const noexcept {
        return name;
      }
      constexpr std::string const& operator()(const SPtr& ptr) const noexcept {
        return ptr->name();
      }
    };
    static constexpr cast_to_key_fn cast_to_key{};

    // Required for RegexUtils helpers - gives ref to stably-stored string key
    static constexpr cast_to_key_fn fb303_key_accessor{};

    using HashBase = folly::HeterogeneousAccessHash<folly::StringPiece>;
    using EqualToBase = folly::HeterogeneousAccessEqualTo<folly::StringPiece>;

    // Use a set with heterogeneous lookup to avoid duplicating the storage of
    // the name, since it is already inside CallbackEntry. The set requires a
    // custom hash function and a custom key-equal function.
    struct Hash : HashBase {
      size_t operator()(
          folly::passable_to<cast_to_key_fn> auto const& val) const noexcept {
        return HashBase::operator()(cast_to_key(val));
      }
    };
    struct EqualTo : EqualToBase {
      bool operator()(
          folly::passable_to<cast_to_key_fn> auto const& lhs,
          folly::passable_to<cast_to_key_fn> auto const& rhs) const noexcept {
        return EqualToBase::operator()(cast_to_key(lhs), cast_to_key(rhs));
      }
    };

    // Both key and string pointer stored in RegexMatchCache refer to name_ in
    // CallbackEntry, so they are stable regardless of map type.
    // Use a vector-set to optimize for iteration in getValues().
    folly::F14VectorSet<SPtr, Hash, EqualTo> map;
    folly::RegexMatchCache matches;
  };

  folly::Synchronized<CallbackMap> callbackMap_;
};

} // namespace fb303
} // namespace facebook

#include <fb303/CallbackValuesMap-inl.h>
