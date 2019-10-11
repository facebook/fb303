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

#include <atomic>
#include <thread>

#include <folly/MicroLock.h>
#include <folly/SharedMutex.h>

namespace facebook {
namespace fb303 {

namespace detail {

/**
 * NoLock is a fake lock that provides no locking.
 */
struct NoLock {
  void lock() {}
  void unlock() {}
};

/**
 * DebugCheckedLock asserts that the lock is only ever acquired from
 * the same thread.
 */
struct DebugCheckedLock {
  void lock() {
    assertOnCorrectThread();
  }

  void unlock() {}

  void assertOnCorrectThread() {
    // The first time we are called, set the thread ID.
    // We do this here rather than in the constructor, since some users
    // create a ThreadLocalStats object in one thread before spawning the
    // thread that will actually use the ThreadLocalStats object.
    if (threadID_ == std::thread::id()) {
      threadID_ = std::this_thread::get_id();
      return;
    }

    assert(threadID_ == std::this_thread::get_id());
  }

  void swapThreads() {
    threadID_ = std::thread::id();
  }

 private:
  std::thread::id threadID_;
};

/**
 * folly::MicroLock does not have a default constructor, so it must be
 * explicitly zeroed ourselves.
 */
class InitedMicroLock : public folly::MicroLock {
 public:
  InitedMicroLock() : folly::MicroLock{} {}
};

} // namespace detail

/**
 * TLStatsNoLocking doesn't perform any locking.
 *
 * Use this for maximum performance when you will only access the
 * ThreadLocalStats object from a single thread.
 *
 * In debug builds, TLStatsNoLocking does check to ensure that it is only used
 * from a single thread, and will assert if you try to access the
 * ThreadLocalStats object from the wrong thread.
 */
class TLStatsNoLocking {
 public:
#ifndef NDEBUG
  using RegistryLock = detail::DebugCheckedLock;
#else
  using RegistryLock = detail::NoLock;
#endif
  using StatLock = detail::NoLock;

  /**
   * The type to use for integer counter values.
   */
  template <typename T>
  class CounterType {
   public:
    CounterType() {}
    explicit CounterType(T n) : value_{n} {}

    void increment(T n) {
      value_ += n;
    }

    T reset() {
      auto tmp = value_;
      value_ = 0;
      return tmp;
    }

   private:
    T value_{0};
  };

  static void willAcquireStatLock(RegistryLock& registryLock) {
#ifndef NDEBUG
    registryLock.assertOnCorrectThread();
#else
    (void)registryLock;
#endif
  }

  static void swapThreads(RegistryLock& lock) {
#ifndef NDEBUG
    lock.swapThreads();
#else
    (void)lock;
#endif
  }
};

/**
 * TLStatsThreadSafe adds locking around accesses to the stat data.
 *
 * When using TLStatsThreadSafe, it is safe to call aggregate() simultaneously
 * with updates to the stat data being made in other threads.  (Where "updates
 * to the stat data" in this case means things like calling addValue() on a
 * TLTimeseriesT or a TLHistogramT, or calling incrementValue() on a
 * TLCounterT).
 *
 * TLStatsThreadSafe also makes it safe to update the stat from multiple
 * threads concurrently, although this is discouraged.  The intended use case
 * is to still have a separate TLStatT object for each thread, so that only one
 * thread is updating the data inside a TLStatT object.  This avoids lock
 * contention, to ensure that stat updates are still fast.
 *
 * However, TLStatsThreadSafe does not synchronize accesses to registration or
 * unregistration of the stat: The caller must still perform their own
 * synchronization around stat object construction, destruction, and
 * removing it from the ThreadLocalStatsT object that it belongs to.  The
 * caller should ensure that no other threads are attempting to update or
 * aggregate the TLStatT object during any operation that registers or
 * unregisters it with a ThreadLocalStatsT container.
 */
class TLStatsThreadSafe {
 public:
  using RegistryLock = folly::SharedMutex;
  using StatLock = detail::InitedMicroLock;

  /**
   * The type to use for integer counter values.
   */
  template <typename T>
  class CounterType {
   public:
    CounterType() {}
    explicit CounterType(T n) : value_{n} {}

    void increment(T n) {
      // It is safe to use std::memory_order_relaxed in this particular case.
      // We are not publishing any other data in memory (besides the counter
      // itself) as a result of incrementing the counter.
      //
      // See more detailed explanation in the last third of this talk from
      // Herb Sutter:
      // https://channel9.msdn.com/Shows/Going+Deep/
      //    Cpp-and-Beyond-2012-Herb-Sutter-atomic-Weapons-2-of-2
      value_.fetch_add(n, std::memory_order_relaxed);
    }

    /**
     * Reset the counter to 0 and return the previous value.
     */
    T reset() {
      // It should probably be safe to use std::memory_order_relaxed here too.
      // We don't expect callers to use extract() to publish any state other
      // than the counter value itself.
      //
      // Nonetheless, extract() should be much less performance sensitive than
      // increment().  We are using memory_order_acq_rel here just to be
      // conservative.
      return value_.exchange(0, std::memory_order_acq_rel);
    }

   private:
    std::atomic<T> value_{0};
  };

  static void willAcquireStatLock(RegistryLock& /*registryLock*/) {}
  static void swapThreads(RegistryLock& /*lock*/) {}
};

} // namespace fb303
} // namespace facebook
