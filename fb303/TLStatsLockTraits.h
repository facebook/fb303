/*
 * Copyright 2019-present Facebook, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
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

#include <folly/SharedMutex.h>
#include <folly/SpinLock.h>
#include <folly/synchronization/SmallLocks.h>

#include <fb303/ThreadLocalStats.h>

namespace facebook {
namespace fb303 {

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
  /*
   * Each individual TLStatT contains a ContainerAndLock object.
   *
   * For TLStatsNoLocking, ContainerAndLock is just a pointer to the container.
   */
  using ContainerAndLock = ThreadLocalStatsT<TLStatsNoLocking>*;

  class StatGuard {
   public:
    explicit StatGuard(const ContainerAndLock* containerAndLock) {
      // To prevent 'containerAndLock' from violating -Wunused-parameter
      // in opt builds.
      (void)containerAndLock;

      // In debug builds, StatGuard checks that the ThreadLocalStats
      // are being accessed from the correct thread.
      //
      // In opt builds asssers are compiled out, and no checking is performed.
      assert(
          (*containerAndLock == nullptr) ||
          (*containerAndLock)->getMainLock()->isInCorrectThread());
    }
  };

  /*
   * MainLock is stored in the ThreadLocalStatsT object.
   *
   * In debug builds, MainLock is used to ensure that all accesses are
   * performed from the correct thread.
   */
  class MainLock {
#ifndef NDEBUG
   public:
    bool isInCorrectThread() const {
      // The first time we are called, set the thread ID.
      // We do this here rather than in the constructor, since some users
      // create a ThreadLocalStats object in one thread before spawning the
      // thread that will actually use the ThreadLocalStats object.
      if (threadID_ == std::thread::id()) {
        threadID_ = std::this_thread::get_id();
        return true;
      }

      return (threadID_ == std::this_thread::get_id());
    }

    void swapThreads() {
      threadID_ = std::thread::id();
    }

   private:
    mutable std::thread::id threadID_;

#else
   public:
    bool isInCorrectThread() const {
      return true;
    }

    void swapThreads() {}
#endif
  };

  class MainGuard {
   public:
    explicit MainGuard(MainLock& lock) {
      // To prevent 'lock' from violating -Wunused-parameter in opt builds.
      (void)lock;

      assert(lock.isInCorrectThread());
    }
  };

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

  static ThreadLocalStatsT<TLStatsNoLocking>* getContainer(
      const ContainerAndLock& containerAndLock) {
    return containerAndLock;
  }

  /**
   * Clear the ThreadLocalStatsT object from a ContainerAndLock.
   *
   * The lock on the ContainerAndLock must be held during this call.
   * The StatGuard parameter is required purely to help ensure that the caller
   * is holding the lock.
   */
  static void clearContainer(
      const StatGuard& /* guard */,
      ContainerAndLock* containerAndLock) {
    *containerAndLock = nullptr;
  }

  /**
   * Set the ThreadLocalStatsT container.
   *
   * The container should currently be null when initContainer() is called.
   * The StatGuard parameter is required purely to help ensure that the caller
   * is holding the lock when calling this method.
   */
  static void initContainer(
      const StatGuard& /* guard */,
      ContainerAndLock* containerAndLock,
      ThreadLocalStatsT<TLStatsNoLocking>* container) {
    DCHECK(!*containerAndLock);
    DCHECK(container);
    *containerAndLock = container;
  }

  static void swapThreads(MainLock* lock) {
    lock->swapThreads();
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
template <typename C>
class TLStatsThreadSafeT {
 public:
  using ContainerAndLock = C;

  class StatGuard {
   public:
    explicit StatGuard(const ContainerAndLock* containerAndLock)
        : lock_(containerAndLock) {
      lock_->lock();
    }

    ~StatGuard() {
      lock_->unlock();
    }

   private:
    const ContainerAndLock* lock_;
  };

  /*
   * The registry in the main ThreadLocalStatsT object is protected with a
   * spinlock.
   */
  using MainLock = folly::SpinLock;
  using MainGuard = folly::SpinLockGuard;

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

  /**
   * Get the ThreadLocalStatsT object from a ContainerAndLock.
   *
   * The lock on the ContainerAndLock does not need to be held during this
   * call.  (The owner of the TLStatsT is responsible for performing any
   * necessary synchronization around operations that update the container.)
   */
  static ThreadLocalStatsT<TLStatsThreadSafeT>* getContainer(
      const ContainerAndLock& containerAndLock) {
    return containerAndLock.getContainer();
  }

  /**
   * Clear the ThreadLocalStatsT object from a ContainerAndLock.
   *
   * The lock on the ContainerAndLock must be held during this call.
   * The StatGuard parameter is required purely to help ensure that the caller
   * is holding the lock.
   */
  static void clearContainer(
      const StatGuard& /* guard */,
      ContainerAndLock* containerAndLock) {
    containerAndLock->clear();
  }

  /**
   * Set the ThreadLocalStatsT container.
   *
   * The container should currently be null when initContainer() is called.
   * The StatGuard parameter is required purely to help ensure that the caller
   * is holding the lock when calling this method.
   */
  static void initContainer(
      const StatGuard& /* guard */,
      ContainerAndLock* containerAndLock,
      ThreadLocalStatsT<TLStatsThreadSafeT>* container) {
    DCHECK(!containerAndLock->getContainer());
    DCHECK(container);
    containerAndLock->init(container);
  }

  static void swapThreads(MainLock* /*lock*/) {}
};

/*
 * ContainerAndLock implementations for TLStatsThreadSafeT.
 * Each individual TLStatT contains a ContainerAndLock object.
 * Currently two thread-safe implementations are supported:
 *
 * 1. ContainerWithPicoSpinLock - uses a PicoSpinLock to store a pointer to the
 * container, while using the least significant bit of the pointer as a spinlock
 *
 * 2. ContainerWithSharedMutex - uses a folly::SharedMutex instead of
 * PicoSpinLock for cases where the spin lock can be inefficient.
 */

class ContainerWithPicoSpinLock {
 public:
  using LockTrait = TLStatsThreadSafeT<ContainerWithPicoSpinLock>;
  using Container = ThreadLocalStatsT<LockTrait>;

  explicit ContainerWithPicoSpinLock(Container* container) {
    static_assert(alignof(Container) >= 2, "Low bit is used for the spin lock");
    value_.init(reinterpret_cast<intptr_t>(container));
  }

  Container* getContainer() const {
    return reinterpret_cast<Container*>(value_.getData());
  }

  void lock() const {
    value_.lock();
  }
  void unlock() const {
    value_.unlock();
  }

  void clear() {
    value_.setData(0);
  }
  void init(Container* container) {
    value_.setData(reinterpret_cast<intptr_t>(container));
  }

 private:
  folly::PicoSpinLock<intptr_t, 0> value_;
};

class ContainerWithSharedMutex {
 public:
  using LockTrait = TLStatsThreadSafeT<ContainerWithSharedMutex>;
  using Container = ThreadLocalStatsT<LockTrait>;

  explicit ContainerWithSharedMutex(Container* container)
      : container_(container) {}

  Container* getContainer() const {
    return container_;
  }

  void lock() const {
    lock_.lock();
  }
  void unlock() const {
    lock_.unlock();
  }

  void clear() {
    container_ = nullptr;
  }
  void init(Container* container) {
    container_ = container;
  }

 private:
  Container* container_ = nullptr;
  mutable folly::SharedMutex lock_;
};

using TLStatsThreadSafe = TLStatsThreadSafeT<ContainerWithPicoSpinLock>;
using TLStatsWithSharedMutex = TLStatsThreadSafeT<ContainerWithSharedMutex>;
} // namespace fb303
} // namespace facebook
