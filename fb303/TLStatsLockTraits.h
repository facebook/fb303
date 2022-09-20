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

#include <atomic>
#include <thread>

#include <folly/Portability.h>
#include <folly/ScopeGuard.h>
#include <folly/SharedMutex.h>
#include <folly/synchronization/DistributedMutex.h>

namespace facebook {
namespace fb303 {

namespace detail {

/**
 * NoLock is a fake lock that provides no locking.
 */
struct UniqueNoLock {
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
  using RegistryLock = std::conditional_t<
      folly::kIsDebug,
      detail::DebugCheckedLock,
      detail::UniqueNoLock>;
  using StatLock = detail::UniqueNoLock;

  /**
   * The type to use for integer counter values.
   */
  template <typename T>
  class CounterType {
   public:
    CounterType() = default;
    explicit CounterType(T n) noexcept : value_{n} {}

    void increment(T n) noexcept {
      value_ += n;
    }

    T reset() noexcept {
      auto tmp = value_;
      value_ = 0;
      return tmp;
    }

    T value() const noexcept {
      return value_;
    }

   private:
    T value_{0};
  };

  /**
   * The type to use for integer timeseries count + sum values.
   */
  template <typename T>
  class TimeSeriesType {
   public:
    TimeSeriesType() = default;
    TimeSeriesType(T count, T sum) noexcept : count_{count}, sum_{sum} {}

    void addValue(T value, T count = 1) noexcept {
      count_ = folly::constexpr_add_overflow_clamped(count_, count);
      sum_ = folly::constexpr_add_overflow_clamped(sum_, value);
    }

    /**
     * Reset the timeseries count + sum to 0 and return the previous value.
     */
    std::pair<T, T> reset() noexcept {
      return {std::exchange(count_, 0), std::exchange(sum_, 0)};
    }

    T count() const noexcept {
      return count_;
    }

    T sum() const noexcept {
      return sum_;
    }

   private:
    T count_{0};
    T sum_{0};
  };

  static void willAcquireStatLock(detail::DebugCheckedLock& registryLock) {
    registryLock.assertOnCorrectThread();
  }
  static void willAcquireStatLock(detail::UniqueNoLock&) {}

  static void swapThreads(detail::DebugCheckedLock& lock) {
    lock.swapThreads();
  }
  static void swapThreads(detail::UniqueNoLock&) {}
};

/**
 * TLStatsThreadSafe uses thread safe data structures or adds locks around
 * accesses to the stat data.
 *
 * When using TLStatsThreadSafe, it is safe to call aggregate() simultaneously
 * with updates to the stat data being made in other threads. (Where "updates
 * to the stat data" in this case means things like calling addValue() on a
 * TLTimeseriesT or a TLHistogramT, or calling incrementValue() on a
 * TLCounterT).
 *
 * TLStatsThreadSafe does NOT makes it safe to update the stat from multiple
 * threads concurrently. The intended use case is to have a separate TLStatT
 * object for each thread, so that only one thread is updating the data inside
 * a TLStatT object. This avoids lock contention, to ensure that stat updates
 * are still fast.
 *
 * Also, TLStatsThreadSafe does not synchronize accesses to registration or
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
  using StatLock = folly::DistributedMutex;

  /**
   * The type to use for integer counter values.
   */
  template <typename T>
  class CounterType {
   public:
    CounterType() = default;
    explicit CounterType(T n) noexcept : value_{n} {}

    void increment(T n) noexcept {
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
    T reset() noexcept {
      // It should probably be safe to use std::memory_order_relaxed here too.
      // We don't expect callers to use extract() to publish any state other
      // than the counter value itself.
      //
      // Nonetheless, extract() should be much less performance sensitive than
      // increment().  We are using memory_order_acq_rel here just to be
      // conservative.
      return value_.exchange(0, std::memory_order_acq_rel);
    }

    T value() const noexcept {
      return value_.load(std::memory_order_relaxed);
    }

   private:
    std::atomic<T> value_{0};
  };

  /**
   * The type to use for integer timeseries count + sum values. addValue()
   * should only be called from a single thread for its lifetime, same applies
   * to count() and sum() accessors
   */
  template <typename T>
  class TimeSeriesType {
   public:
    TimeSeriesType() = default;
    TimeSeriesType(T count, T sum) noexcept {
      state_[side_].count = count;
      state_[side_].sum = sum;
    }

    void addValue(T value, T count = 1) noexcept {
      auto wasLocked = locked_.exchange(true, std::memory_order_seq_cst);
      DCHECK(!wasLocked);
      auto side = side_.load(std::memory_order_seq_cst);
      state_[side].count =
          folly::constexpr_add_overflow_clamped(state_[side].count, count);
      state_[side].sum =
          folly::constexpr_add_overflow_clamped(state_[side].sum, value);
      locked_.store(false, std::memory_order_release);
    }

    /**
     * Reset the timeseries count + sum to 0 and return the previous value.
     */
    std::pair<T, T> reset() noexcept {
      std::unique_lock lock(mutex_);
      auto side = side_.fetch_xor(1, std::memory_order_seq_cst);
      while (locked_.load(std::memory_order_seq_cst)) {
        folly::asm_volatile_pause();
      }

      // reset counters from side, nothing can be writing to them anymore.
      auto [count, sum] = std::exchange(state_[side], {});
      return {count, sum};
    }

    /**
     * Unsafe to call concurrently with reset() or addValue(), only for testing
     */
    T count() const noexcept {
      return state_[side_].count;
    }

    /**
     * Unsafe to call concurrently with reset() or addValue(), only for testing
     */
    T sum() const noexcept {
      return state_[side_].sum;
    }

   private:
    struct State {
      T count{0};
      T sum{0};
    } state_[2];

    // makes stores to state_[side_] in reset() visible to addValue(), top bit
    // is a lock
    std::atomic<uint16_t> side_{0};

    // makes stores to state_[side_] in addValue() visible to reset()
    // serves as a lightweight mutex between add and reset
    std::atomic<bool> locked_{false};

    // Serialize calls to reset()
    folly::SharedMutex mutex_;
  };

  static void willAcquireStatLock(RegistryLock& /*registryLock*/) {}
  static void swapThreads(RegistryLock& /*lock*/) {}
};

} // namespace fb303
} // namespace facebook
