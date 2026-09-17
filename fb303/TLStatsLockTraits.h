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
#include <cstdint>
#include <limits>
#include <thread>
#include <type_traits>
#include <utility>

#include <folly/Portability.h>
#include <folly/SharedMutex.h>
#include <folly/portability/Asm.h>
#include <folly/synchronization/DistributedMutex.h>
#include <folly/synchronization/RelaxedAtomic.h>

namespace facebook::fb303 {

namespace detail {

/**
 * NoLock is a fake lock that provides no locking.
 */
struct UniqueNoLock {
  void lock() {}
  bool try_lock() {
    return true;
  }
  void unlock() {}
};

/**
 * DebugCheckedLock asserts that the lock is not acquired concurrently by
 * multiple threads.
 */
struct DebugCheckedLock {
  void lock() {
    [[maybe_unused]] auto old =
        owner_.exchange(std::this_thread::get_id(), std::memory_order_acq_rel);
    assert(old == std::thread::id{});
  }

  bool try_lock() {
    std::thread::id expected{};
    return owner_.compare_exchange_strong(
        expected, std::this_thread::get_id(), std::memory_order_acq_rel);
  }

  void unlock() {
    owner_.store(std::thread::id{}, std::memory_order_release);
  }

 private:
  std::atomic<std::thread::id> owner_;
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
   *
   * reset() reports the count and sum accumulated since the previous reset().
   * Neither wraps: both clamp on every add, so a total that overflows T and
   * comes back reports the limit rather than its true value.
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
 *
 * Timeseries aggregation, where this differs from TLStatsNoLocking:
 *  - the count and sum reported by reset() always describe the same updates
 *  - neither wraps; the exact total is clamped once, at reset(), so a total
 *    that overflows T and comes back reports its true value rather than the
 *    limit
 *  - a window may be reported one reset() late if an update is in flight;
 *    nothing is lost or double-counted
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
   * The type to use for integer timeseries count + sum values.
   *
   * addValue() must be called from one thread only, for the lifetime of the
   * object, and count() and sum() must be called from that same thread.
   * reset() writes only the baselines, and may be called from any number of
   * threads concurrently with the adder.
   */
  template <typename T>
  class TimeSeriesType {
    static_assert(std::is_integral_v<T>, "TimeSeriesType requires an integer");
    static_assert(std::is_signed_v<T>, "TimeSeriesType requires a signed type");

    using Raw = std::make_unsigned_t<T>;

    // Net wraps of the low word, so it has to be signed: a window that wraps
    // up and back nets to zero and the delta stays exact. 64 bits, so the
    // carry cannot itself wrap.
    using Carry = int64_t;

    // Bit 0 marks a sequence as in flight: set in seq_ while addValue() is
    // part-way through an update, and in lastSeq_ while a drain holds the
    // claim. Published sequences are even, so the bit is free in both.
    static constexpr uint64_t kInFlight = 1;

    static constexpr int kSpinsBeforeYield = 128;

    static void backOff(int& spins) noexcept {
      if (spins < kSpinsBeforeYield) {
        ++spins;
        folly::asm_volatile_pause();
      } else {
        std::this_thread::yield();
      }
    }

    // The addend is already reduced mod 2^64, with its sign passed alongside,
    // so a caller can add a value that a signed T cannot hold.
    //
    // Release: orders the odd sequence before the totals, so a reader cannot
    // re-read an even sequence and accept a half-written one.
    static void addRaw(
        std::atomic<Raw>& lo,
        std::atomic<Carry>& hi,
        Raw addend,
        bool negative) noexcept {
      const Raw before = lo.load(std::memory_order_relaxed);
      const Raw after = before + addend;
      lo.store(after, std::memory_order_release);
      if (FOLLY_UNLIKELY(negative ? after > before : after < before)) {
        // Unsigned, so there is no signed overflow to reason about. The carry
        // moves by one per wrap of the low word, so reaching a limit of Carry
        // would take 2^63 of them and cannot happen.
        const uint64_t curr =
            static_cast<uint64_t>(hi.load(std::memory_order_relaxed));
        const uint64_t step = negative ? ~uint64_t{0} : uint64_t{1};
        hi.store(static_cast<Carry>(curr + step), std::memory_order_release);
      }
    }

    static void
    add(std::atomic<Raw>& lo, std::atomic<Carry>& hi, T addend) noexcept {
      addRaw(lo, hi, static_cast<Raw>(addend), addend < 0);
    }

    struct Snapshot {
      Raw lo{0};
      // Carry, not T: a narrower T would silently truncate it.
      Carry hi{0};

      friend bool operator==(Snapshot a, Snapshot b) noexcept {
        return a.lo == b.lo && a.hi == b.hi;
      }
    };

    struct Surplus {
      std::atomic<Raw> lo{0};
      std::atomic<Carry> hi{0};
      folly::relaxed_atomic<Raw> baseLo{0};
      folly::relaxed_atomic<Carry> baseHi{0};
    };

   public:
    TimeSeriesType() = default;

    ~TimeSeriesType() {
      // Externally synchronised (class docblock): nobody else is in here.
      delete surplus_.load(std::memory_order_relaxed);
    }

    TimeSeriesType(const TimeSeriesType&) = delete;
    TimeSeriesType& operator=(const TimeSeriesType&) = delete;
    TimeSeriesType(TimeSeriesType&&) = delete;
    TimeSeriesType& operator=(TimeSeriesType&&) = delete;

    void addValue(T value, T count = 1) noexcept {
      const uint64_t seq = seq_.load(std::memory_order_relaxed);
      DCHECK_EQ(seq & kInFlight, 0u)
          << "Concurrent addValue() calls are not allowed";

      Surplus* surplus = nullptr;
      if (FOLLY_UNLIKELY(count != 1)) {
        surplus = surplus_.load(std::memory_order_relaxed);
        if (FOLLY_UNLIKELY(surplus == nullptr)) {
          surplus = makeSurplus();
        }
      }

      seq_.store(seq + 1, std::memory_order_relaxed);
      if (FOLLY_UNLIKELY(surplus != nullptr)) {
        // The sequence already counts this call as one, so the block carries
        // the rest. Reduced first: count - 1 is signed overflow at the
        // minimum of T.
        addRaw(
            surplus->lo, surplus->hi, static_cast<Raw>(count) - 1, count < 1);
      }
      add(sumLo_, sumHi_, value);
      seq_.store(seq + 2, std::memory_order_release);
    }

    /**
     * Reset the timeseries count + sum to 0 and return the previous value.
     */
    std::pair<T, T> reset() noexcept {
      if (seq_.load(std::memory_order_relaxed) ==
          (lastSeq_.load(std::memory_order_relaxed) & ~kInFlight)) {
        return {};
      }
      return drain();
    }

    /**
     * The window so far. Unsafe to call concurrently with either addValue() or
     * reset(). These loads are not bracketed by the sequence the way drain()'s
     * are, so they can mix words from either side of an update, or a total
     * with a baseline a drain has since replaced.
     */
    T count() const noexcept {
      // Mask the claim: a concurrent drain makes lastSeq_ odd, and an odd
      // subtrahend here underflows to ~2^63 calls and reports INT64_MAX.
      const uint64_t seq = seq_.load(std::memory_order_relaxed);
      const uint64_t last = lastSeq_.load(std::memory_order_relaxed);
      const uint64_t calls = (seq - (last & ~kInFlight)) / 2;
      const Surplus* block = surplus_.load(std::memory_order_acquire);
      const Snapshot total =
          block == nullptr ? Snapshot{} : loadTotal(block->lo, block->hi);
      const Snapshot base = block == nullptr
          ? Snapshot{}
          : loadBase(block->baseLo, block->baseHi);
      return clamp(addCalls(difference(total, base), calls));
    }

    /// Same caveat as count().
    T sum() const noexcept {
      return clamp(difference(
          loadTotal(sumLo_, sumHi_), loadBase(baseSumLo_, baseSumHi_)));
    }

   private:
    FOLLY_NOINLINE Surplus* makeSurplus() noexcept {
      // nothrow: addValue() is noexcept. On failure the update counts as one
      // call whatever count was; the sum is unaffected and a later call
      // allocates again.
      auto* surplus = new (std::nothrow) Surplus();
      if (FOLLY_LIKELY(surplus != nullptr)) {
        // Release: seeing the pointer must imply seeing the zeroed words.
        surplus_.store(surplus, std::memory_order_release);
      }
      return surplus;
    }

    FOLLY_NOINLINE std::pair<T, T> drain() noexcept {
      uint64_t last = lastSeq_.load(std::memory_order_relaxed);
      for (int spins = 0;;) {
        if (FOLLY_UNLIKELY(last & kInFlight)) {
          backOff(spins);
          last = lastSeq_.load(std::memory_order_relaxed);
          continue;
        }
        // Acquire: the baselines read below are the ones the previous drain
        // wrote, ordered by its release store of lastSeq_.
        if (FOLLY_LIKELY(lastSeq_.compare_exchange_weak(
                last,
                last | kInFlight,
                std::memory_order_acquire,
                std::memory_order_relaxed))) {
          break;
        }
      }

      const Snapshot baseSum = loadBase(baseSumLo_, baseSumHi_);
      const auto [surplus, sum, seq, block] = readTotals();
      const Snapshot baseSurplus = block == nullptr
          ? Snapshot{}
          : loadBase(block->baseLo, block->baseHi);
      const uint64_t calls = (seq - last) / 2;
      if (block != nullptr) {
        storeBase(block->baseLo, block->baseHi, surplus);
      }
      storeBase(baseSumLo_, baseSumHi_, sum);
      // Even, so it releases the claim; ordered after the baselines.
      lastSeq_.store(seq, std::memory_order_release);
      return {
          clamp(addCalls(difference(surplus, baseSurplus), calls)),
          clamp(difference(sum, baseSum))};
    }

    // Pairs with the release stores in add(); the acquire is what makes the
    // sequence check sufficient.
    static Snapshot loadTotal(
        const std::atomic<Raw>& lo,
        const std::atomic<Carry>& hi) noexcept {
      return Snapshot{
          lo.load(std::memory_order_acquire),
          hi.load(std::memory_order_acquire)};
    }

    static Snapshot loadBase(
        const folly::relaxed_atomic<Raw>& lo,
        const folly::relaxed_atomic<Carry>& hi) noexcept {
      return Snapshot{lo, hi};
    }

    static void storeBase(
        folly::relaxed_atomic<Raw>& lo,
        folly::relaxed_atomic<Carry>& hi,
        Snapshot value) noexcept {
      lo = value.lo;
      hi = value.hi;
    }

    // Two halves rather than __int128: MSVC lacks it, and fb303 ships to
    // Windows through getdeps.
    static Snapshot difference(Snapshot now, Snapshot base) noexcept {
      const Raw lo = now.lo - base.lo;
      const uint64_t borrow = now.lo < base.lo ? uint64_t{1} : uint64_t{0};
      // Modular in the carry's width, so a wrapped carry subtracts as the
      // 128-bit value it stands for would.
      const Carry hi = static_cast<Carry>(
          static_cast<uint64_t>(now.hi) - static_cast<uint64_t>(base.hi) -
          borrow);
      return Snapshot{lo, hi};
    }

    static Snapshot addCalls(Snapshot delta, uint64_t calls) noexcept {
      Raw lo;
      uint64_t carry;
      if constexpr (sizeof(Raw) < sizeof(uint64_t)) {
        // A narrow Raw can wrap more than once, and a single-wrap test would
        // keep only the last. Exact in 64 bits: delta.lo is below Raw's
        // modulus.
        constexpr unsigned kRawBits = sizeof(Raw) * 8;
        const uint64_t sum = static_cast<uint64_t>(delta.lo) + calls;
        lo = static_cast<Raw>(sum);
        carry = sum >> kRawBits;
      } else {
        lo = static_cast<Raw>(delta.lo + calls);
        carry = lo < delta.lo ? uint64_t{1} : uint64_t{0};
      }
      const Carry hi =
          static_cast<Carry>(static_cast<uint64_t>(delta.hi) + carry);
      return Snapshot{lo, hi};
    }

    // Reads (hi, lo) as two's complement: hi is 0 in [0, 2^64) and -1 in
    // [-2^64, 0), anything else is past a limit, and within them lo decides.
    static T clamp(Snapshot delta) noexcept {
      constexpr auto kMaxAsRaw =
          static_cast<Raw>(std::numeric_limits<T>::max());
      if (delta.hi > 0) {
        return std::numeric_limits<T>::max();
      }
      if (delta.hi == 0) {
        return delta.lo > kMaxAsRaw ? std::numeric_limits<T>::max()
                                    : static_cast<T>(delta.lo);
      }
      if (delta.hi == -1) {
        // Representable only once lo is at or past 2^63; below that the true
        // value is under the signed minimum.
        return delta.lo > kMaxAsRaw ? static_cast<T>(delta.lo)
                                    : std::numeric_limits<T>::min();
      }
      return std::numeric_limits<T>::min();
    }

    struct Read {
      Snapshot surplus;
      Snapshot sum;
      uint64_t seq{};
      Surplus* block{};
    };

    Read readTotals() const noexcept {
      for (int spins = 0;;) {
        const uint64_t before = seq_.load(std::memory_order_acquire);
        if (before & kInFlight) {
          backOff(spins);
          continue;
        }
        // Inside the loop: a pinned stale null would accept a snapshot
        // reporting no surplus for a window that has one.
        Surplus* const block = surplus_.load(std::memory_order_acquire);
        const Snapshot count =
            block == nullptr ? Snapshot{} : loadTotal(block->lo, block->hi);
        const Snapshot sum = loadTotal(sumLo_, sumHi_);
        // Relaxed: the totals above are acquire loads, so this one cannot be
        // hoisted above them, which is all the confirmation needs.
        if (seq_.load(std::memory_order_relaxed) == before) {
          return Read{count, sum, before, block};
        }
      }
    }

    std::atomic<Raw> sumLo_{0};
    std::atomic<uint64_t> seq_{0};
    std::atomic<Carry> sumHi_{0};

    std::atomic<uint64_t> lastSeq_{0};
    folly::relaxed_atomic<Raw> baseSumLo_{0};
    folly::relaxed_atomic<Carry> baseSumHi_{0};

    // Out of line because only a count other than 1 needs it: inline, every
    // stat on every thread would carry 16 bytes it never uses. Never freed
    // before destruction, so a non-null value read under the sequence stays
    // valid.
    std::atomic<Surplus*> surplus_{nullptr};
  };
};

} // namespace facebook::fb303
