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

#include <fb303/ThreadLocalStats.h>

#include <fb303/ServiceData.h>
#include <fb303/ThreadCachedServiceData.h>
#include <folly/Singleton.h>
#include <folly/synchronization/test/Barrier.h>
#include <folly/test/TestUtils.h>

#include <gflags/gflags.h>
#include <gtest/gtest.h>
#include <limits>
#include <memory>
#include <random>
#include <string>
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

const ExportedStat::Duration kTimeseriesIntervalsC[] = {
    std::chrono::seconds(5),
    std::chrono::seconds(15)};

template <typename LockTraits>
void testSaturateTimeseries() {
  using lim = std::numeric_limits<int64_t>;
  std::string const key = "dummy";
  ServiceData data;
  ThreadLocalStatsT<LockTraits> tlstats(&data);
  TLTimeseriesT<LockTraits> stat{&tlstats, key, SUM, AVG, COUNT};
  EXPECT_EQ(0, stat.count());
  EXPECT_EQ(0, stat.sum());

  stat.addValue(lim::max());
  EXPECT_EQ(1, stat.count());
  EXPECT_EQ(lim::max(), stat.sum());
  stat.addValue(1);
  EXPECT_EQ(2, stat.count());
  EXPECT_EQ(lim::max(), stat.sum());

  stat.addValueAggregated(0, lim::max() - 2);
  EXPECT_EQ(lim::max(), stat.count());
  EXPECT_EQ(lim::max(), stat.sum());
  stat.addValue(0);
  EXPECT_EQ(lim::max(), stat.count());
  EXPECT_EQ(lim::max(), stat.sum());
}

TEST(ThreadLocalStats, SaturateTimeseries) {
  {
    SCOPED_TRACE("TLStatsThreadSafe");
    testSaturateTimeseries<TLStatsThreadSafe>();
  }
  {
    SCOPED_TRACE("TLStatsNoLocking");
    testSaturateTimeseries<TLStatsNoLocking>();
  }
}

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
  auto tlstats = std::make_unique<ThreadLocalStatsT<LockTraits>>(&data);

  TLCounterT<LockTraits> ctr1{tlstats.get(), "foo"};
  TLHistogramT<LockTraits> ctr2{
      tlstats.get(), "bar", 1, 20, 30, SUM, COUNT, 50};
  TLTimeseriesT<LockTraits> ctr3{tlstats.get(), "bar", SUM, COUNT};

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
  auto tlstats = std::make_unique<ThreadLocalStatsT<LockTraits>>(&data);

  TLCounterT<LockTraits> ctr1{&*tlstats, "foo"};
  TLHistogramT<LockTraits> ctr2{&*tlstats, "bar", 1, 20, 30, SUM, COUNT, 50};
  TLTimeseriesT<LockTraits> ctr3{tlstats.get(), "bar", SUM, COUNT};

  tlstats.reset();

  TLCounterT<LockTraits> ctr4{std::move(ctr1)};
  TLHistogramT<LockTraits> ctr5{std::move(ctr2)};
  TLTimeseriesT<LockTraits> ctr6{std::move(ctr3)};
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
  std::unique_ptr<ThreadLocalStatsT<TLStatsThreadSafe>> container;
  std::unique_ptr<TLCounterT<TLStatsThreadSafe>> counter;
  std::unique_ptr<TLTimeseriesT<TLStatsThreadSafe>> timeseries;
  std::unique_ptr<TLHistogramT<TLStatsThreadSafe>> histogram;
};

TEST(ThreadLocalStats, stressStatDestructionRace) {
  ServiceData data;

  folly::test::Barrier gate(FLAGS_num_threads * 4 + 1);

  std::vector<std::thread> threads;
  for (gflags::int32 i = 0; i < FLAGS_num_threads; ++i) {
    auto c = std::make_shared<ContainerAndStats>();
    c->container =
        std::make_unique<ThreadLocalStatsT<TLStatsThreadSafe>>(&data);
    c->counter = std::make_unique<TLCounterT<TLStatsThreadSafe>>(
        c->container.get(), "stat1");
    c->timeseries = std::make_unique<TLTimeseriesT<TLStatsThreadSafe>>(
        c->container.get(), "stat2", SUM, COUNT);
    c->histogram = std::make_unique<TLHistogramT<TLStatsThreadSafe>>(
        c->container.get(), "stat3", 1, 20, 30, SUM, COUNT, 50);

    threads.emplace_back([&, c] {
      gate.wait();
      c->container.reset();
    });
    threads.emplace_back([&, c] {
      gate.wait();
      c->counter.reset();
    });
    threads.emplace_back([&, c] {
      gate.wait();
      c->timeseries.reset();
    });
    threads.emplace_back([&, c] {
      gate.wait();
      c->histogram.reset();
    });
  }

  gate.wait();

  for (auto& thread : threads) {
    thread.join();
  }
}

TEST(ThreadLocalStats, handOffBetweenThreads) {
  using ThreadLocalStats = ThreadLocalStatsT<TLStatsNoLocking>;
  using TLTimeseries = TLTimeseriesT<TLStatsNoLocking>;

  ServiceData serviceData;
  std::unique_ptr<ThreadLocalStats> container;

  std::unique_ptr<TLTimeseries> t;

  auto thread = std::thread([&] {
    container = std::make_unique<ThreadLocalStats>(&serviceData);
    t = std::make_unique<TLTimeseries>(container.get(), "foo", SUM, COUNT);
    t->addValue(1);
  });

  thread.join();

  // Access to t is serialized because thread is joined.

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

TEST(ThreadLocalStats, timeseriesCopy) {
  using ThreadLocalStats = ThreadLocalStatsT<TLStatsThreadSafe>;
  using TLTimeseries = TLTimeseriesT<TLStatsThreadSafe>;

  ServiceData serviceData;

  ThreadLocalStats tlStats1{&serviceData};
  ThreadLocalStats tlStats2{&serviceData};

  TLTimeseries ts1{&tlStats1, "bar", SUM, COUNT};
  EXPECT_EQ(ts1.name(), "bar");
  EXPECT_FALSE(ts1.getGlobalStat().isNull());
  TLTimeseries ts2{&tlStats2, ts1};
  EXPECT_FALSE(ts2.getGlobalStat().isNull());
  EXPECT_EQ(ts2.name(), "bar");

  ts1.addValue(3);
  ts2.addValue(2);

  tlStats1.aggregate();
  EXPECT_EQ(serviceData.getCounter("bar.sum"), 3);
  EXPECT_EQ(serviceData.getCounter("bar.count"), 1);

  tlStats2.aggregate();
  EXPECT_EQ(serviceData.getCounter("bar.sum"), 5);
  EXPECT_EQ(serviceData.getCounter("bar.count"), 2);
}

class ThreadCachedServiceDataTest : public testing::Test {
 protected:
  void SetUp() override {
    ThreadCachedServiceData::get()->startPublishThread(
        std::chrono::milliseconds(10));
    ThreadCachedServiceData::get()->addStatExportType("dummy", SUM);
  }

  void TearDown() override {
    ThreadCachedServiceData::get()->getServiceData()->resetAllData();
    ThreadCachedServiceData::get()->getThreadStats()->resetAllData();
    ThreadCachedServiceData::get()->stopPublishThread();
  }

  static bool waitCounterToUpdate(
      folly::StringPiece counter,
      int64_t expectedValue,
      std::chrono::milliseconds time) {
    auto now = std::chrono::steady_clock::now();
    while (expectedValue != fbData->getCounter(counter)) {
      sched_yield();
      if (std::chrono::steady_clock::now() - now > time) {
        return false;
      }
    }
    return true;
  }
  static auto ms(int64_t value) {
    return std::chrono::milliseconds(value);
  }
};

TEST_F(ThreadCachedServiceDataTest, PublishThreadRestartsWithGet) {
  folly::SingletonVault::singleton()->destroyInstances();
  folly::SingletonVault::singleton()->reenableInstances();

  EXPECT_EQ(0, fbData->getCounter("dummy.sum"));
  ThreadCachedServiceData::get()->addStatValue("dummy");
  EXPECT_TRUE(waitCounterToUpdate("dummy.sum", 1, ms(200)));
}

TEST_F(ThreadCachedServiceDataTest, PublishThreadRestartsWithGetShared) {
  folly::SingletonVault::singleton()->destroyInstances();
  folly::SingletonVault::singleton()->reenableInstances();

  EXPECT_EQ(0, fbData->getCounter("dummy.sum"));
  ThreadCachedServiceData::getShared()->addStatValue("dummy");
  EXPECT_TRUE(waitCounterToUpdate("dummy.sum", 1, ms(200)));
}

TEST_F(ThreadCachedServiceDataTest, AlwaysAvailable) {
  folly::SingletonVault::singleton()->destroyInstances();
  ThreadCachedServiceData::get()->addStatValue("dummy");
  folly::SingletonVault::singleton()->reenableInstances();
}

TEST_F(ThreadCachedServiceDataTest, StoppedPublisherDoesNotRestart) {
  ThreadCachedServiceData::get()->stopPublishThread();
  EXPECT_EQ(false, ThreadCachedServiceData::get()->publishThreadRunning());

  folly::SingletonVault::singleton()->destroyInstances();
  ThreadCachedServiceData::get()->addStatValue("dummy");
  folly::SingletonVault::singleton()->reenableInstances();

  EXPECT_EQ(0, fbData->getCounter("dummy.sum"));
  EXPECT_EQ(false, ThreadCachedServiceData::get()->publishThreadRunning());
  ThreadCachedServiceData::get()->addStatValue("dummy");
  EXPECT_FALSE(waitCounterToUpdate("dummy.sum", 1, ms(200)));
}

TEST_F(ThreadCachedServiceDataTest, SaturateTimeseries) {
  using lim = std::numeric_limits<int64_t>;
  auto& tcsd = *ThreadCachedServiceData::get();
  std::string const key = "dummy";
  tcsd.addStatValue(key, lim::max());
  tcsd.addStatValue(key, 1);
  tcsd.publishStats();
  EXPECT_EQ(lim::max(), tcsd.getCounter(key + ".sum"));
}

TEST_F(ThreadCachedServiceDataTest, AddHistogramValueNotExported) {
  std::random_device rng;
  std::random_device::result_type nums[8];
  std::generate(std::begin(nums), std::end(nums), std::ref(rng));
  auto const key = "key:" +
      folly::hexlify<std::string>(folly::ByteRange(
          reinterpret_cast<uint8_t const*>(std::begin(nums)),
          reinterpret_cast<uint8_t const*>(std::end(nums))));
  auto& sd = *ServiceData::get();
  auto& tcsd = *ThreadCachedServiceData::get();
  auto& map = *sd.getHistogramMap();
  auto& stats = *tcsd.getThreadStats();

  // sanity
  EXPECT_TRUE(map.getLockableHistogram(key).isNull());
  EXPECT_FALSE(stats.getHistogramSafe(key));

  // not exported
  tcsd.addHistogramValue(key, 1); // should not crash
  EXPECT_TRUE(map.getLockableHistogram(key).isNull());
  EXPECT_FALSE(stats.getHistogramSafe(key));

  // exported - sanity
  ServiceData::get()->addHistogram(key, 1, 0, 4);
  EXPECT_FALSE(map.getLockableHistogram(key).isNull());
  EXPECT_TRUE(stats.getHistogramSafe(key));
}

// Test that completePendingLink correctly updates tlStatsEmpty_ flag
TEST(ThreadLocalStats, CompletePendingLinkUpdatesEmptyFlag) {
  ServiceData data;
  ThreadLocalStatsT<TLStatsThreadSafe> tlstats(&data);

  // Initially empty - aggregate should return 0
  EXPECT_EQ(0, tlstats.aggregate());

  // Create a stat - it will be registered directly or via pending list
  TLCounterT<TLStatsThreadSafe> counter(&tlstats, "test_counter");
  counter.incrementValue(42);

  // Aggregate should find the stat (either from main list or pending list)
  uint64_t count = tlstats.aggregate();
  EXPECT_EQ(1, count);

  // Verify the counter value was aggregated
  EXPECT_EQ(42, data.getCounter("test_counter"));

  // Aggregate again - should still process the stat from main list
  count = tlstats.aggregate();
  EXPECT_EQ(1, count);
}

// Test concurrent registration via pending stats list
TEST(ThreadLocalStats, ConcurrentPendingRegistration) {
  ServiceData data;
  ThreadLocalStatsT<TLStatsThreadSafe> tlstats(&data);

  constexpr int kNumThreads = 10;
  constexpr int kIncrementValue = 7;

  std::vector<std::thread> threads;
  threads.reserve(kNumThreads);
  folly::test::Barrier startBarrier(kNumThreads + 1);

  for (int i = 0; i < kNumThreads; ++i) {
    threads.emplace_back([&, i]() {
      // Wait for all threads to be ready
      startBarrier.wait();

      // Create stats that may contend for the lock
      std::string counterName = "counter_" + std::to_string(i);
      TLCounterT<TLStatsThreadSafe> counter(&tlstats, counterName);
      counter.incrementValue(kIncrementValue);

      // Aggregate locally
      counter.aggregate();
    });
  }

  // Start all threads at once
  startBarrier.wait();

  // Wait for all threads to complete
  for (auto& thread : threads) {
    thread.join();
  }

  // Aggregate to drain any remaining pending stats
  tlstats.aggregate();

  // Verify all counters were registered and aggregated
  for (int i = 0; i < kNumThreads; ++i) {
    std::string counterName = "counter_" + std::to_string(i);
    EXPECT_EQ(kIncrementValue, data.getCounter(counterName))
        << "Counter " << counterName << " has wrong value";
  }
}

// Test stat registration, aggregation, and destruction lifecycle
TEST(ThreadLocalStats, StatLifecycle) {
  ServiceData data;
  ThreadLocalStatsT<TLStatsThreadSafe> tlstats(&data);

  // Initially empty - aggregate should return quickly
  EXPECT_EQ(0, tlstats.aggregate());

  // Add a stat
  {
    TLCounterT<TLStatsThreadSafe> counter(&tlstats, "temp_counter");
    counter.incrementValue(1);

    // Aggregate should find the stat
    EXPECT_EQ(1, tlstats.aggregate());
    EXPECT_EQ(1, data.getCounter("temp_counter"));
  }

  // After stat is destroyed, aggregate should still work
  EXPECT_EQ(0, tlstats.aggregate());
}
