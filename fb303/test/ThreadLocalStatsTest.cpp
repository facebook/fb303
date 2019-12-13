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

#include <fb303/ThreadLocalStats.h>
#include <folly/synchronization/test/Barrier.h>
#include <folly/test/TestUtils.h>

#include <gflags/gflags.h>
#include <gtest/gtest.h>
#include <future>
#include <thread>

using namespace facebook::fb303;

DEFINE_int32(
    num_threads,
    20,
    "The number of worker threads concurrently updating stats");
DEFINE_int32(
    duration,
    3,
    "How long to run the ConcurrentOperations test, in seconds");

enum : uint64_t {
  HIST_INCR = 11,
  TIMESERIES_A_INCR1 = 3,
  TIMESERIES_A_INCR2 = 5,
  TIMESERIES_B_INCR = 7,
  TIMESERIES_C_INCR = 9,
  COUNTER_INCR = 13,
};

const int kTimeseriesIntervalsC[] = {5, 15};

class WorkerThread {
 public:
  WorkerThread(ServiceData* serviceData, std::atomic<bool>* stop)
      : stop_(stop), stats_(serviceData), thread_([this] { this->run(); }) {}

  void aggregate() {
    stats_.aggregate();
  }

  void join() {
    thread_.join();
  }

  uint64_t getNumIterations() const {
    return numIters_;
  }

 private:
  void run() {
    while (!stop_->load()) {
      ++numIters_;

      TLHistogramT<TLStatsThreadSafe> hist(
          &stats_, "histogram", 10, 0, 1000, AVG, COUNT, SUM, 50, 95, 99);
      hist.addValue(HIST_INCR);

      TLTimeseriesT<TLStatsThreadSafe> tsA(
          &stats_, "timeseriesA", AVG, COUNT, SUM);
      tsA.addValue(TIMESERIES_A_INCR1);
      tsA.addValue(TIMESERIES_A_INCR2);

      TLTimeseriesT<TLStatsThreadSafe> tsB(
          &stats_, "timeseriesB", AVG, COUNT, SUM, RATE);
      tsB.addValue(TIMESERIES_B_INCR);

      TLTimeseriesT<TLStatsThreadSafe> tsC(
          &stats_,
          "timeseriesC",
          static_cast<size_t>(60),
          static_cast<size_t>(2),
          kTimeseriesIntervalsC);
      tsC.addValue(TIMESERIES_C_INCR);

      TLCounterT<TLStatsThreadSafe> c(&stats_, "counter");
      c.incrementValue(COUNTER_INCR);
    }
  }

 private:
  uint64_t numIters_{0};
  std::atomic<bool>* stop_{nullptr};
  ThreadLocalStatsT<TLStatsThreadSafe> stats_;
  std::thread thread_;
};

// Test one thread calling aggregate() in a loop, while other threads
// create, increment, and destroy thread local stats
TEST(ThreadSafeStats, ConcurrentOperations) {
  ServiceData data;

  // Start N threads, each of which will loop creating, updating, and
  // destroying thread local stats objects
  std::atomic<bool> stop(false);
  std::vector<std::unique_ptr<WorkerThread>> threads;
  for (gflags::int32 n = 0; n < FLAGS_num_threads; ++n) {
    threads.emplace_back(new WorkerThread(&data, &stop));
  }

  // Loop for N seconds, calling aggregate() on the thread local
  // stats in all threads
  auto start = std::chrono::steady_clock::now();
  auto end = start + std::chrono::seconds(FLAGS_duration);
  while (std::chrono::steady_clock::now() < end) {
    for (const auto& thread : threads) {
      thread->aggregate();
    }
  }

  // Stop all of the threads
  stop.store(true);
  uint64_t numIters = 0;
  for (const auto& thread : threads) {
    thread->join();
    numIters += thread->getNumIterations();
  }
  threads.clear();

  LOG(INFO) << "Ran " << numIters << " iterations across " << FLAGS_num_threads
            << " threads";

  // Verify that the global counters are now what we expect
  EXPECT_EQ(numIters * HIST_INCR, data.getCounter("histogram.sum"));
  EXPECT_EQ(numIters, data.getCounter("histogram.count"));
  EXPECT_EQ(HIST_INCR, data.getCounter("histogram.avg"));

  EXPECT_EQ(
      numIters * (TIMESERIES_A_INCR1 + TIMESERIES_A_INCR2),
      data.getCounter("timeseriesA.sum"));
  EXPECT_EQ(numIters * 2, data.getCounter("timeseriesA.count"));
  EXPECT_EQ(
      (TIMESERIES_A_INCR1 + TIMESERIES_A_INCR2) / 2.0,
      data.getCounter("timeseriesA.avg"));

  EXPECT_EQ(numIters * TIMESERIES_B_INCR, data.getCounter("timeseriesB.sum"));
  EXPECT_EQ(numIters, data.getCounter("timeseriesB.count"));
  EXPECT_EQ(TIMESERIES_B_INCR, data.getCounter("timeseriesB.avg"));

  EXPECT_EQ(numIters * COUNTER_INCR, data.getCounter("counter"));
}

template <typename LockTraits>
void testMoveTimeseries() {
  ServiceData data;
  ThreadLocalStatsT<LockTraits> tlstats(&data);

  {
    TLTimeseriesT<LockTraits> stat1{&tlstats, "foo", SUM, COUNT};
    stat1.addValue(1);

    // Move construction
    TLTimeseriesT<LockTraits> stat2{std::move(stat1)};
    stat2.addValue(2);

    TLTimeseriesT<LockTraits> stat3{&tlstats, "bar", SUM, COUNT};
    stat3.addValue(3);

    // Move assignment
    stat3 = std::move(stat2);
    stat3.addValue(4);
  }

  EXPECT_EQ(3, data.getCounter("foo.count"));
  EXPECT_EQ(7, data.getCounter("foo.sum"));
  EXPECT_EQ(1, data.getCounter("bar.count"));
  EXPECT_EQ(3, data.getCounter("bar.sum"));
}

TEST(ThreadLocalStats, moveTimeseries) {
  {
    SCOPED_TRACE("TLStatsThreadSafe");
    testMoveTimeseries<TLStatsThreadSafe>();
  }
  {
    SCOPED_TRACE("TLStatsNoLocking");
    testMoveTimeseries<TLStatsNoLocking>();
  }
}

template <typename LockTraits>
void testMoveHistogram() {
  ServiceData data;
  ThreadLocalStatsT<LockTraits> tlstats(&data);

  {
    // Use bucket width of 0, and a range of 0 to 1000a
    // and export SUM, COUNT, and p50 counters.
    TLHistogramT<LockTraits> hist1{
        &tlstats, "foo", 10, 0, 1000, SUM, COUNT, 50};
    hist1.addValue(15);

    // Move construction
    TLHistogramT<LockTraits> hist2{std::move(hist1)};
    hist2.addValue(44);
    hist2.addValue(75);

    TLHistogramT<LockTraits> hist3{&tlstats, "bar", 1, 20, 30, SUM, COUNT, 50};
    hist3.addValue(23);

    // Move assignment
    hist3 = std::move(hist2);
    hist3.addValue(46);
  }

  EXPECT_EQ(4, data.getCounter("foo.count"));
  EXPECT_EQ(180, data.getCounter("foo.sum"));
  EXPECT_EQ(45, data.getCounter("foo.p50"));

  EXPECT_EQ(1, data.getCounter("bar.count"));
  EXPECT_EQ(23, data.getCounter("bar.sum"));
  EXPECT_EQ(23, data.getCounter("bar.p50"));
}

TEST(ThreadLocalStats, moveHistogram) {
  {
    SCOPED_TRACE("TLStatsThreadSafe");
    testMoveHistogram<TLStatsThreadSafe>();
  }
  {
    SCOPED_TRACE("TLStatsNoLocking");
    testMoveHistogram<TLStatsNoLocking>();
  }
}

template <typename LockTraits>
void testMoveCounter() {
  ServiceData data;
  ThreadLocalStatsT<LockTraits> tlstats(&data);

  {
    TLCounterT<LockTraits> ctr1{&tlstats, "foo"};
    ctr1.incrementValue(1);

    // Move construction
    TLCounterT<LockTraits> ctr2{std::move(ctr1)};
    ctr2.incrementValue(2);

    TLCounterT<LockTraits> ctr3{&tlstats, "bar"};
    ctr3.incrementValue(3);

    // Move assignment
    ctr3 = std::move(ctr2);
    ctr3.incrementValue(4);
  }

  EXPECT_EQ(7, data.getCounter("foo"));
  EXPECT_EQ(3, data.getCounter("bar"));
}

TEST(ThreadLocalStats, moveCounter) {
  {
    SCOPED_TRACE("TLStatsThreadSafe");
    testMoveCounter<TLStatsThreadSafe>();
  }
  {
    SCOPED_TRACE("TLStatsNoLocking");
    testMoveCounter<TLStatsNoLocking>();
  }
}

template <typename LockTraits>
void testDestroyContainerBeforeStat() {
  ServiceData data;
  std::optional<ThreadLocalStatsT<LockTraits>> tlstats{std::in_place, &data};

  TLCounterT<LockTraits> ctr1{&*tlstats, "foo"};
  TLHistogramT<LockTraits> ctr2{&*tlstats, "bar", 1, 20, 30, SUM, COUNT, 50};

  tlstats.reset();
}

TEST(ThreadLocalStats, destroyThreadContainerBeforeStat) {
  {
    SCOPED_TRACE("TLStatsThreadSafe");
    testDestroyContainerBeforeStat<TLStatsThreadSafe>();
  }
  {
    SCOPED_TRACE("TLStatsNoLocking");
    testDestroyContainerBeforeStat<TLStatsNoLocking>();
  }
}

template <typename LockTraits>
void testDestroyContainerBeforeMovingStat() {
  ServiceData data;
  std::optional<ThreadLocalStatsT<LockTraits>> tlstats{std::in_place, &data};

  TLCounterT<LockTraits> ctr1{&*tlstats, "foo"};
  TLHistogramT<LockTraits> ctr2{&*tlstats, "bar", 1, 20, 30, SUM, COUNT, 50};

  tlstats.reset();

  TLCounterT<LockTraits> ctr3{std::move(ctr1)};
  TLHistogramT<LockTraits> ctr4{std::move(ctr2)};
}

TEST(ThreadLocalStats, destroyContainerBeforeMovingStat) {
  {
    SCOPED_TRACE("TLStatsThreadSafe");
    testDestroyContainerBeforeMovingStat<TLStatsThreadSafe>();
  }
  {
    SCOPED_TRACE("TLStatsNoLocking");
    testDestroyContainerBeforeMovingStat<TLStatsNoLocking>();
  }
}

struct ContainerAndStats {
  std::optional<ThreadLocalStatsT<TLStatsThreadSafe>> container;
  std::optional<TLCounterT<TLStatsThreadSafe>> stat1;
  std::optional<TLCounterT<TLStatsThreadSafe>> stat2;
  std::optional<TLCounterT<TLStatsThreadSafe>> stat3;
};

TEST(ThreadLocalStats, stressStatDestructionRace) {
  ServiceData data;

  folly::test::Barrier gate(FLAGS_num_threads * 4);

  std::vector<std::thread> threads;
  for (gflags::int32 i = 0; i < FLAGS_num_threads; ++i) {
    auto c = std::make_shared<ContainerAndStats>();
    c->container.emplace(&data);
    c->stat1.emplace(&c->container.value(), "stat1");
    c->stat2.emplace(&c->container.value(), "stat2");
    c->stat3.emplace(&c->container.value(), "stat3");

    threads.emplace_back([&, c] {
      gate.wait();
      c->container.reset();
    });
    threads.emplace_back([&, c] {
      gate.wait();
      c->stat1.reset();
    });
    threads.emplace_back([&, c] {
      gate.wait();
      c->stat2.reset();
    });
    threads.emplace_back([&, c] {
      gate.wait();
      c->stat3.reset();
    });
  }

  for (auto& thread : threads) {
    thread.join();
  }
}

TEST(ThreadLocalStats, handOffBetweenThreads) {
  using ThreadLocalStats = ThreadLocalStatsT<TLStatsNoLocking>;
  using TLTimeseries = TLTimeseriesT<TLStatsNoLocking>;

  ServiceData serviceData;

  std::optional<TLTimeseries> t;

  auto f = std::async(std::launch::async, [&] {
    auto p = std::make_unique<ThreadLocalStats>(&serviceData);
    t = TLTimeseries{p.get(), "foo", SUM, COUNT};
    t->addValue(1);
    return p;
  });

  auto container = f.get();
  container->swapThreads();

  t->addValue(2);

  // Try registering a new stat.
  auto u = TLTimeseries{container.get(), "bar", SUM, COUNT};
  u.addValue(3);

  // This test asserts that handoff between threads does not assert or
  // throw an exception.
}

template <typename LockTraits>
void moveHistogramAcrossContainers() {
  ServiceData data1;
  ThreadLocalStatsT<LockTraits> tlstats1(&data1);

  ServiceData data2;
  ThreadLocalStatsT<LockTraits> tlstats2(&data2);

  TLHistogramT<LockTraits> hist1{&tlstats1, "foo", 10, 0, 1000, SUM, COUNT, 50};
  hist1.addValue(10);

  TLHistogramT<LockTraits> hist2{&tlstats2, "foo", 10, 0, 1000, SUM, COUNT, 50};
  hist2.addValue(20);

  hist1 = std::move(hist2);
  hist1.addValue(30);

  tlstats1.aggregate();
  tlstats2.aggregate();

  hist1.addValue(40);
  tlstats1.aggregate();
  tlstats2.aggregate();

  EXPECT_EQ(10, data1.getCounter("foo.sum"));
  EXPECT_EQ(90, data2.getCounter("foo.sum"));
}

TEST(ThreadLocalStats, moveStatFromOneContainerToAnother) {
  {
    SCOPED_TRACE("TLStatsThreadSafe");
    moveHistogramAcrossContainers<TLStatsThreadSafe>();
  }
  {
    SCOPED_TRACE("TLStatsNoLocking");
    moveHistogramAcrossContainers<TLStatsNoLocking>();
  }
}
