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

/**
 * This test reproduces a TSAN data race in ThreadLocalStats between:
 *
 * 1. Thread calling TLStatT::link() which, when failing to acquire the lock,
 *    reads tlStats_.empty() without holding the lock (line 86 in
 *    ThreadLocalStats-inl.h)
 *
 * 2. Thread calling aggregate() -> completePendingLink() which inserts into
 *    tlStats_ while holding the lock (line 539 in ThreadLocalStats-inl.h)
 *
 * The race occurs because the check on line 86:
 *   if (pendingList->size() == 1 && link_->container_->tlStats_.empty())
 * reads from tlStats_ without synchronization while another thread may be
 * modifying it in completePendingLink().
 */

#include <fb303/ThreadLocalStats.h>

#include <fb303/ServiceData.h>
#include <fmt/core.h>
#include <folly/Portability.h>
#include <folly/synchronization/test/Barrier.h>

#include <gtest/gtest.h>
#include <atomic>
#include <memory>
#include <thread>
#include <vector>

using namespace facebook::fb303;

/**
 * Test that creates a race condition between:
 * - Multiple threads creating TLTimeseries objects (calling link())
 * - A thread continuously calling aggregate() (which calls completePendingLink)
 *
 * The race occurs when:
 * 1. Thread A is in link() and fails try_lock()
 * 2. Thread A reads tlStats_.empty() without the lock
 * 3. Thread B is in completePendingLink() inserting into tlStats_
 */
TEST(ThreadLocalStatsLinkRace, LinkWhileAggregating) {
  ServiceData data;

  // Reduce iterations for TSAN mode which is much slower
  constexpr int kNumIterations = folly::kIsSanitizeThread ? 100 : 1000;
  constexpr int kNumWriterThreads = 10;

  for (int iter = 0; iter < kNumIterations; ++iter) {
    auto container =
        std::make_shared<ThreadLocalStatsT<TLStatsThreadSafe>>(&data);

    std::atomic<bool> stop{false};
    std::atomic<int> statCount{0};

    // Thread that continuously calls aggregate(), which will call
    // completePendingLink() and modify tlStats_
    std::thread aggregatorThread([&] {
      while (!stop.load(std::memory_order_relaxed)) {
        container->aggregate();
      }
    });

    // Multiple threads creating stats, which call link()
    // When try_lock() fails, link() reads tlStats_.empty() without the lock
    std::vector<std::thread> writerThreads;
    writerThreads.reserve(kNumWriterThreads);
    for (int i = 0; i < kNumWriterThreads; ++i) {
      writerThreads.emplace_back([&, i] {
        // Create multiple stats to increase contention
        for (int j = 0; j < 10; ++j) {
          auto name = fmt::format("stat_{}_{}_{}", iter, i, j);
          TLTimeseriesT<TLStatsThreadSafe> ts(container.get(), name, SUM);
          ts.addValue(1);
          statCount.fetch_add(1, std::memory_order_relaxed);
        }
      });
    }

    // Wait for writers to finish
    for (auto& t : writerThreads) {
      t.join();
    }

    // Stop aggregator
    stop.store(true, std::memory_order_relaxed);
    aggregatorThread.join();

    // Verify all stats were created (the test is about the race, not the count)
    EXPECT_EQ(statCount.load(), kNumWriterThreads * 10);
  }
}

/**
 * This test creates maximum contention on the link()/aggregate() path.
 * It uses barriers to synchronize threads to increase the likelihood
 * of hitting the race window.
 */
TEST(ThreadLocalStatsLinkRace, HighContentionLinkRace) {
  ServiceData data;

  // Reduce iterations for TSAN mode which is much slower
  constexpr int kNumIterations = folly::kIsSanitizeThread ? 50 : 500;
  constexpr int kNumThreads = 20;

  for (int iter = 0; iter < kNumIterations; ++iter) {
    auto container =
        std::make_shared<ThreadLocalStatsT<TLStatsThreadSafe>>(&data);

    // Use a barrier to synchronize all threads to maximize contention
    folly::test::Barrier barrier(kNumThreads + 1);

    std::vector<std::thread> threads;
    threads.reserve(kNumThreads);

    // Half the threads will create stats (calling link())
    for (int i = 0; i < kNumThreads / 2; ++i) {
      threads.emplace_back([&, i] {
        barrier.wait();
        for (int j = 0; j < 20; ++j) {
          auto name = fmt::format("ts_{}_{}_{}", iter, i, j);
          TLTimeseriesT<TLStatsThreadSafe> ts(container.get(), name, SUM);
          ts.addValue(1);
        }
      });
    }

    // Half the threads will call aggregate() (which calls completePendingLink)
    for (int i = 0; i < kNumThreads / 2; ++i) {
      threads.emplace_back([&] {
        barrier.wait();
        for (int j = 0; j < 100; ++j) {
          container->aggregate();
        }
      });
    }

    // Release all threads at once
    barrier.wait();

    for (auto& t : threads) {
      t.join();
    }

    // Verify stats were created and aggregated correctly
    container->aggregate();
    auto counters = data.getCounters();
    for (int i = 0; i < kNumThreads / 2; ++i) {
      for (int j = 0; j < 20; ++j) {
        auto name = fmt::format("ts_{}_{}_{}.sum", iter, i, j);
        EXPECT_EQ(counters.count(name), 1) << "Missing stat: " << name;
      }
    }
  }
}

/**
 * This test specifically targets the code path where try_lock() fails
 * and link() accesses tlStats_.empty() without synchronization.
 *
 * We create a scenario where:
 * 1. One thread holds the lock for a longer time (via aggregate())
 * 2. Other threads try to create stats and fail to acquire the lock
 * 3. When they fail, they read tlStats_.empty() while the lock holder
 *    may be modifying tlStats_
 */
TEST(ThreadLocalStatsLinkRace, TryLockFailurePath) {
  ServiceData data;

  // Reduce iterations for TSAN mode which is much slower
  constexpr int kNumIterations = folly::kIsSanitizeThread ? 100 : 1000;

  for (int iter = 0; iter < kNumIterations; ++iter) {
    auto container =
        std::make_shared<ThreadLocalStatsT<TLStatsThreadSafe>>(&data);

    // Pre-populate with some stats to ensure tlStats_ is not empty
    // This makes the empty() check more interesting
    std::vector<std::unique_ptr<TLTimeseriesT<TLStatsThreadSafe>>> preStats;
    preStats.reserve(5);
    for (int i = 0; i < 5; ++i) {
      preStats.emplace_back(
          std::make_unique<TLTimeseriesT<TLStatsThreadSafe>>(
              container.get(), fmt::format("pre_stat_{}", i), SUM));
    }

    std::atomic<bool> stop{false};
    folly::test::Barrier startBarrier(3);

    // Thread 1: Continuously hold the lock via aggregate()
    std::thread aggregator([&] {
      startBarrier.wait();
      while (!stop.load(std::memory_order_relaxed)) {
        container->aggregate();
      }
    });

    // Thread 2: Create many stats rapidly, many will fail try_lock()
    // and read tlStats_.empty() without the lock
    std::thread creator1([&] {
      startBarrier.wait();
      for (int i = 0; i < 100; ++i) {
        auto name = fmt::format("stat1_{}_{}", iter, i);
        TLTimeseriesT<TLStatsThreadSafe> ts(container.get(), name, SUM);
        ts.addValue(1);
      }
      stop.store(true, std::memory_order_relaxed);
    });

    // Thread 3: Also create stats to increase contention
    std::thread creator2([&] {
      startBarrier.wait();
      for (int i = 0; i < 100; ++i) {
        auto name = fmt::format("stat2_{}_{}", iter, i);
        TLTimeseriesT<TLStatsThreadSafe> ts(container.get(), name, SUM);
        ts.addValue(1);
      }
    });

    aggregator.join();
    creator1.join();
    creator2.join();

    // Verify all stats were created and aggregated correctly
    container->aggregate();
    auto counters = data.getCounters();
    // Check pre-populated stats
    for (int i = 0; i < 5; ++i) {
      auto name = fmt::format("pre_stat_{}.sum", i);
      EXPECT_EQ(counters.count(name), 1) << "Missing pre-stat: " << name;
    }
    // Check dynamically created stats from both creators
    for (int i = 0; i < 100; ++i) {
      auto name1 = fmt::format("stat1_{}_{}.sum", iter, i);
      auto name2 = fmt::format("stat2_{}_{}.sum", iter, i);
      EXPECT_EQ(counters.count(name1), 1) << "Missing stat1: " << name1;
      EXPECT_EQ(counters.count(name2), 1) << "Missing stat2: " << name2;
    }
  }
}
