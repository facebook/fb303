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

#include <cstdint>
#include <list>
#include <stdexcept>
#include <unordered_map>

#include <glog/logging.h>

namespace facebook {
namespace fb303 {

// TODO: Allow the choice between std::list, std::vector or other containers
// through traits - T1974246

template <
    typename TKey,
    typename TValue,
    template <typename, typename, typename...> class TMap = std::unordered_map,
    typename TStats = std::uint_fast32_t,
    typename TRatio = double,
    typename... TMapArgs>
struct SimpleLRUMap {
  typedef typename std::remove_const<
      typename std::remove_reference<TKey>::type>::type key_type;
  typedef TValue mapped_type;
  typedef std::pair<const key_type, mapped_type> value_type;
  typedef TStats stats_type;
  typedef TRatio ratio_type;

 private:
  typedef std::list<value_type> list_type;
  typedef TMap<key_type, typename list_type::iterator, TMapArgs...> map_type;

 public:
  typedef typename list_type::size_type size_type;
  typedef typename list_type::const_iterator const_iterator;
  typedef typename list_type::iterator iterator;

 private:
  size_type capacity_;
  list_type list_;
  size_type listSize_;
  map_type map_;
  mutable stats_type hits_;
  mutable stats_type misses_;

  struct NoOpCallback {
    void operator()(value_type&&) {}
  };

  template <typename callback_type>
  void evict(callback_type evictCallback) {
    DCHECK_GT(listSize_, 0);
    map_.erase(list_.back().first);

    evictCallback(std::move(list_.back()));
    list_.pop_back();
    --listSize_;
  }

  template <typename callback_type>
  bool ensure_room(callback_type evictCallback) {
    if (capacity_ < 1) {
      return false;
    }

    while (size() >= capacity_) {
      evict(evictCallback);
    }

    return true;
  }

  iterator splay(iterator i) {
    list_.splice(list_.begin(), list_, i);
    return list_.begin();
  }

  template <typename callback_type>
  iterator try_add(
      const key_type& key,
      mapped_type&& value,
      callback_type evictCallback) {
    if (!ensure_room(evictCallback)) {
      return list_.end();
    }

    list_.emplace_front(key, std::move(value));
    ++listSize_;
    map_.emplace(key, list_.begin());

    return list_.begin();
  }

 public:
  explicit SimpleLRUMap(size_type capacity = 0)
      : capacity_(capacity), listSize_(0), hits_(0), misses_(0) {}

  void reserve(size_type size) {
    map_.reserve(std::min(capacity_, size));
  }

  // does not move to front
  // throws std::out_of_range when not found
  const mapped_type& peek(const key_type& key) const {
    auto i = find(key);

    if (i == end()) {
      throw std::out_of_range("key not found");
    }

    return i->second;
  }

  // does move to front
  // throws std::out_of_range when not found
  mapped_type& touch(const key_type& key) {
    auto i = find(key, true);

    if (i == end()) {
      throw std::out_of_range("key not found");
    }

    return i->second;
  }

  // if the element doesn't exist, `value_factory` will be
  //  used to create it
  // `value_factory` receives the key as its only parameter
  // if there is an eviction, the callback will be called
  //  with an r-value reference of the evicted entry
  // note: there may be more than one eviction depending on
  //  the underlying implementation
  // returns nullptr if there is no capacity
  template <typename value_factory, typename callback_type = NoOpCallback>
  mapped_type* try_get_or_create(
      const key_type& key,
      value_factory factory,
      bool moveToFront = true,
      callback_type evictCallback = callback_type()) {
    auto i = find(key, moveToFront);

    if (i != end()) {
      return std::addressof(i->second);
    }

    auto j = try_add(key, factory(key), evictCallback);

    if (j == list_.end()) {
      return nullptr;
    }

    return std::addressof(j->second);
  }

  // if there is an eviction, the callback will be called
  //  with an r-value reference of the evicted entry
  // note: there may be more than one eviction depending on
  //  the underlying implementation
  // if the element doesn't exist, the default constructor
  //  will be used to create it
  // returns nullptr if there is no capacity
  template <typename callback_type = NoOpCallback>
  mapped_type* try_get_or_create(
      const key_type& key,
      bool moveToFront = true,
      callback_type evictCallback = callback_type()) {
    return try_get_or_create(
        key,
        [](const key_type&) { return mapped_type(); },
        moveToFront,
        evictCallback);
  }

  // if the element doesn't exist, `value_factory` will be
  //  used to create it
  // `value_factory` receives the key as its only parameter
  // if there is an eviction, the callback will be called
  //  with an r-value reference of the evicted entry
  // note: there may be more than one eviction depending on
  //  the underlying implementation
  // throws std::length_error if there is no capacity
  template <typename value_factory, typename callback_type = NoOpCallback>
  mapped_type& get_or_create(
      const key_type& key,
      value_factory factory,
      bool moveToFront = true,
      callback_type evictCallback = callback_type()) {
    if (auto p = try_get_or_create(key, factory, moveToFront, evictCallback)) {
      return *p;
    }

    throw std::length_error("no capacity");
  }

  // if there is an eviction, the callback will be called
  //  with an r-value reference of the evicted entry
  // note: there may be more than one eviction depending on
  //  the underlying implementation
  // if the element doesn't exist, the default constructor
  //  will be used to create it
  // throws std::length_error if there is no capacity
  template <typename callback_type = NoOpCallback>
  mapped_type& get_or_create(
      const key_type& key,
      bool moveToFront = true,
      callback_type evictCallback = callback_type()) {
    return get_or_create(
        key,
        [](const key_type&) { return mapped_type(); },
        moveToFront,
        evictCallback);
  }

  // if there is an eviction, the callback will be called
  //  with an r-value reference of the evicted entry
  // note: there may be more than one eviction depending on
  //  the underlying implementation
  // `moveToFront` applies only to existing keys
  // returns non-zero on success or zero if there was not enough capacity
  // when successful, returns 1 if a new element was created
  //  or -1 if the size remained unchanged
  template <typename callback_type = NoOpCallback>
  int try_set(
      const key_type& key,
      mapped_type value,
      bool moveToFront = true,
      callback_type evictCallback = callback_type()) {
    auto i = map_.find(key);

    if (i == map_.end()) {
      if (try_add(key, std::move(value), evictCallback) == list_.end()) {
        return 0;
      }

      return 1;
    }

    if (moveToFront) {
      i->second = splay(i->second);
    }

    i->second->second = std::move(value);
    return -1;
  }

  // if there is an eviction, the callback will be called
  //  with an r-value reference of the evicted entry
  // note: there may be more than one eviction depending on
  //  the underlying implementation
  // `moveToFront` applies only to existing keys
  // returns a bool telling whether a new element was created
  // throws std::length_error if there is no capacity
  template <typename callback_type = NoOpCallback>
  bool set(
      const key_type& key,
      mapped_type value,
      bool moveToFront = true,
      callback_type evictCallback = callback_type()) {
    switch (try_set(key, std::move(value), moveToFront, evictCallback)) {
      case 1:
        return true;

      case -1:
        return false;

      default:
        throw std::length_error("no capacity");
    }
  }

  // does not move to front
  const_iterator find(const key_type& key) const {
    auto i = map_.find(key);

    if (i == map_.end()) {
      ++misses_;
      return end();
    }

    ++hits_;
    return i->second;
  }

  iterator find(const key_type& key, bool moveToFront) {
    auto i = map_.find(key);

    if (i == map_.end()) {
      ++misses_;
      return end();
    }

    if (moveToFront) {
      i->second = splay(i->second);
    }

    ++hits_;
    return i->second;
  }

  bool erase(const key_type& key) {
    auto i = map_.find(key);

    if (i == map_.end()) {
      return false;
    }

    list_.erase(i->second);
    DCHECK_GT(listSize_, 0);
    listSize_--;
    map_.erase(i);

    return true;
  }

  // must be a valid iterator, not end()
  iterator erase(const_iterator pos) {
    DCHECK_GT(listSize_, 0);
    auto i = map_.find(pos->first);

    if (i == map_.end()) {
      return list_.end();
    }

    auto next = list_.erase(i->second);
    --listSize_;
    map_.erase(i);

    return next;
  }

  // does not move to front
  // throws std::out_of_range when not found
  const mapped_type& operator[](const key_type& key) const {
    return peek(key);
  }

  // does not move to front
  // throws std::out_of_range when not found
  mapped_type& operator[](const key_type& key) {
    auto i = find(key, false);

    if (i == end()) {
      throw std::out_of_range("key not found");
    }

    return i->second;
  }

  iterator begin() {
    return list_.begin();
  }
  iterator end() {
    return list_.end();
  }
  const_iterator begin() const {
    return list_.begin();
  }
  const_iterator end() const {
    return list_.end();
  }
  const_iterator cbegin() const {
    return list_.cbegin();
  }
  const_iterator cend() const {
    return list_.cend();
  }

  void clear(bool clearStats = true) {
    map_.clear();
    list_.clear();
    listSize_ = 0;

    if (clearStats) {
      clear_stats();
    }
  }

  bool empty() const {
    DCHECK_EQ(listSize_ == 0, list_.empty());
    return list_.empty();
  }

  size_type size() const {
    return listSize_;
  }

  size_type capacity() const {
    return capacity_;
  }

  // if there is an eviction, the callback will be called
  //  with an r-value reference of the evicted entry
  // note: there may be more than one eviction depending on
  //  the underlying implementation
  template <typename callback_type = NoOpCallback>
  size_type capacity(
      size_type newCapacity,
      callback_type evictCallback = callback_type()) {
    auto oldCapacity = capacity_;

    for (auto i = size(); newCapacity < i; --i) {
      evict(evictCallback);
    }

    capacity_ = newCapacity;

    return oldCapacity;
  }

  stats_type hits() const {
    return hits_;
  }
  stats_type misses() const {
    return misses_;
  }
  ratio_type hit_ratio() const {
    const auto total = hits_ + misses_;

    if (total == 0) {
      return 0;
    }

    return static_cast<ratio_type>(hits_) / (total);
  }

  void clear_stats() {
    hits_ = 0;
    misses_ = 0;
  }
};

} // namespace fb303
} // namespace facebook
