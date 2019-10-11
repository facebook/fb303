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
#include <folly/experimental/StringKeyedUnorderedMap.h>
#include <memory>
#include <mutex>
#include <unordered_map>

namespace facebook {
namespace fb303 {

template <class K, class V>
struct SynchMapTraits {
  typedef K KeyType;
  typedef V ValueType;
  typedef const K& LookupType;
  typedef std::pair<std::shared_ptr<std::mutex>, std::shared_ptr<ValueType>>
      LockAndItem;
  typedef std::unordered_map<K, LockAndItem> MapType;
};

template <class V>
struct SynchMapTraits<std::string, V> {
  typedef std::string KeyType;
  typedef V ValueType;
  typedef folly::StringPiece LookupType;
  typedef std::pair<std::shared_ptr<std::mutex>, std::shared_ptr<ValueType>>
      LockAndItem;
  using MapType = folly::StringKeyedUnorderedMap<LockAndItem>;
};

/**
 * SynchMap is a hash map that implements thread safety
 * by having a global mutex for the hash structure itself, as well
 * as a mutex that protects every value in the map.
 *
 * The get() call locks the value's mutex and returns a special shared_ptr<>
 * that, instead of deleting the value, releases the mutex when it is
 * destroyed.
 *
 * Note that currently this is a very simple wrapper, and thus doesn't expose
 * any iterators or other functionality.
 *
 * Also note if you delete items from this map, the items will be removed
 * right away but they will get destructed after the last reference to them
 * is deleted.
 *
 * Example:
 *
 *   SynchMap<int, string> map;
 *
 *   // -------
 *   // set two values
 *   // -------
 *   map.set(10, string("10"));
 *   map.set(20, string("20"));
 *
 *   // -------
 *   // lock a key and edit its contents
 *   // -------
 *   shared_ptr<string> ptr = map.get(10);   // lock for key == 10 is now held
 *   if (ptr) {
 *     *ptr = "X";
 *   }
 *   ptr.reset();  // release the lock on key == 10
 *
 *   // -------
 *   // alternate way to use a nested scope to lock a key
 *   // -------
 *   {
 *     shared_ptr<string> ptr = map.get(10);  // locks the key
 *     ...
 *   }  // key is unlocked here
 *
 *
 * TODO(mrabkin): Add iterators
 * TODO(mrabkin): Add multi-operation locking so you can keep map locked
 *   and do several lookups in a row to reduce contention
 */
template <class K, class V, class T = SynchMapTraits<K, V>>
class SynchMap {
 public: // type definitions
  typedef T Traits;
  typedef typename Traits::LookupType LookupType;
  typedef typename Traits::KeyType KeyType;
  typedef typename Traits::ValueType ValueType;

  /**
   * The MutexReleaser class is a special shared_ptr deallocator that,
   * instead of calling 'delete' on the given ptr, instead releases an
   * associated lock.
   *
   * Note: Each instance of this is designed to only be used ONCE!  Make sure
   * you make a new MutexReleaser() instance for each shared_ptr you create
   * (except, of course, when copy-constructing shared_ptr).
   */
  class MutexReleaser {
   public:
    explicit MutexReleaser() : lock_(nullptr), ptr_(nullptr) {}

    explicit MutexReleaser(
        std::shared_ptr<std::mutex> lock,
        std::shared_ptr<V> ptr)
        : lock_(std::move(lock)), ptr_(std::move(ptr)) {}

    void operator()(V* /*ptr*/) {
      if (lock_) {
        lock_->unlock();
      }
      lock_.reset();
      ptr_.reset();
    }

   private:
    std::shared_ptr<std::mutex> lock_;
    std::shared_ptr<V> ptr_;
  };

  typedef std::shared_ptr<ValueType> LockedValuePtr;

  // special unique_ptr<> that holds lock while it exists
  typedef std::unique_ptr<ValueType, MutexReleaser> UniqueValuePtr;

  // pair of map item and its associated lock
  typedef std::pair<std::shared_ptr<std::mutex>, std::shared_ptr<ValueType>>
      LockAndItem;

 public:
  SynchMap();
  ~SynchMap();

  /**
   * Checks if the map contains key.  Note that this state might change
   * at any time (immediately) after returning.
   */
  bool contains(LookupType key) const;

  /**
   * Returns a shared_ptr to the value stored in the map for the given key, and
   * locks the per-value mutex associated with that value.  Modifiying the
   * object pointed to by this shared_ptr will modify the value stored in the
   * map.  When the returned shared_ptr (and all of its copies, if any) are
   * reset, the per-value mutex is released.  In other words, a thread
   * will maintain exclusive control over the given key until it resets
   * (or destroys) the returned shared_ptr.
   *
   * If the value isn't present in the map, an empty shared_ptr is returned
   * and no locks are held.
   *
   * --
   * NOTE: Do _not_ store the LockedValuePtr for a long time.  This will
   * keep the mutex locked and not allow anyone else to access this item.
   * --
   *
   */
  LockedValuePtr get(LookupType key);

  /**
   * Behaves identically to get(), except that if the value is missing from the
   * map, an entry is created with the defaultVal provided and then returned.
   * V must be copy-constructible.  If createdPtr is not NULL, its contents are
   * set to 'true' if an item was just created and 'false' otherwise.
   *
   * --
   * NOTE: Do _not_ store the LockedValuePtr for a long time.  This will
   * keep the mutex locked and not allow anyone else to access this item.
   * --
   */
  LockedValuePtr getOrCreate(
      LookupType key,
      const ValueType& defaultVal,
      bool* createdPtr = nullptr);

  /**
   * Get an item if it exists in the map, or create it if it does not.
   *
   * @param key         The key to lookup or create.
   * @param createdPtr  Used to indicate if a new item was created or not.
   * @param args        The arguments to pass to the value constructor if a new
   *                    item needs to be created.
   */
  template <typename... Args>
  LockedValuePtr emplace(LookupType key, bool* createdPtr, Args&&... args);

  /**
   * If the item exists in the map, returns a regular vanilla shared_ptr to
   * the item, and the item's associated spinlock.  This allows users to manage
   * their own locking of the item.  If the item is missing, a pair with NULL
   * shared_ptrs<> for the item and lock is returned.
   *
   * --
   * NOTE: You _must_ lock the std::mutex with a std::lock_guard before
   * accessing the value of the item.
   * --
   */
  LockAndItem getUnlocked(LookupType key);

  /**
   * Behaves identically to getUnlocked(), except that if the value is missing
   * from the map, an entry is created with the defaultVal provided and then
   * returned.  V must be copy-constructible.  If createdPtr is not NULL, its
   * contents are set to 'true' if an item was just created and 'false'
   * otherwise.
   *
   * --
   * NOTE: You _must_ lock the mutex with a std::lock_guard before accessing
   * the value of the item.
   * --
   */
  LockAndItem getOrCreateUnlocked(
      LookupType key,
      const ValueType& defaultVal,
      bool* createdPtr = nullptr);

  /**
   * Get an item if it exists in the map, or create it if it does not.
   *
   * @param key         The key to lookup or create.
   * @param createdPtr  Used to indicate if a new item was created or not.
   * @param args        The arguments to pass to the value constructor if a new
   *                    item needs to be created.
   *
   * --
   * NOTE: You _must_ lock the mutex with a std::lock_guard before accessing
   * the value of the item.
   * --
   */
  template <typename... Args>
  LockAndItem emplaceUnlocked(LookupType key, bool* createdPtr, Args&&... args);

  /**
   * Same as createUniqueValuePtr, but creates a shared pointer, which does
   * reference counting. Avoid using this method if possible.
   */
  static LockedValuePtr createLockedValuePtr(
      LockAndItem* item,
      folly::SharedMutex::ReadHolder* guard = nullptr) {
    UniqueValuePtr ret = createUniqueValuePtr(item, guard);

    return LockedValuePtr(std::move(ret));
  }

  /**
   * Given an item, returned by getUnlocked() or getOrCreatedUnlocked(), locks
   * its associated lock and returns a UniqueValuePtr that will release the
   * lock when the pointer (and any copies that may have been made) are
   * destroyed.
   *
   * This can be used to convert items returned by the 'Unlocked' versions of
   * the getters to ones returned by the regular get() and getOrCreate().
   */

  static UniqueValuePtr createUniqueValuePtr(
      LockAndItem* item,
      folly::SharedMutex::ReadHolder* guard = nullptr);

  /**
   * Call a function on each element in the map.
   *
   * The function prototype should be:
   *   fn(const LookupType& key, ValueType& value)
   *
   * Note that the SynchMap lock is held for the duration of the forEach()
   * call, so the function should not invoke any other modifying operations on
   * this SynchMap, as that would result in deadlock.
   */
  template <class Fn>
  void forEach(const Fn& fn);

  /**
   * Sets the value associated with the given key.  Note that no locks are held
   * after the set() is complete, so the value might change at any time
   * (immediately) after returning.
   */
  void set(LookupType key, const ValueType& val);

  /**
   * Erases the key value pair for the given key.
   */
  bool erase(LookupType key);

  /**
   * Remove all entries from the map.
   */
  void clear();

 private:
  typedef typename Traits::MapType MapType;

  MapType map_;
  folly::SharedMutex lock_;
};

} // namespace fb303
} // namespace facebook

#include <fb303/SynchMap-inl.h>
