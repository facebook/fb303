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

#include <fb303/ThreadCachedServiceData.h>

#include <folly/Indestructible.h>
#include <folly/Singleton.h>

using std::chrono::milliseconds;

namespace {
static const std::string kFunctionId =
    "ThreadCachedStatsMap::aggregateAcrossAllThreads";
} // namespace

namespace facebook {
namespace fb303 {

DEFINE_timeseries(fb303_tcData_publish_time_usec, SUM, AVG);
DEFINE_timeseries(fb303_tcData_aggregate_call_count, SUM);
DEFINE_timeseries(fb303_tcData_tlmaps_aggregated, SUM);

class PublisherManager {
 public:
  struct Worker {
    folly::FunctionScheduler fs_;
    Worker() {
      fs_.addFunction(
          [] { ThreadCachedServiceData::getInternal().publishStats(); },
          ThreadCachedServiceData::getInternal().getPublisherInterval(),
          kFunctionId);
      fs_.setThreadName("servicedata-pub");
      fs_.start();
    }
  };
  folly::Synchronized<folly::Optional<Worker>> worker_;
  PublisherManager() {
    if (ThreadCachedServiceData::getInternal().publishThreadRunning()) {
      // Singleton was shutdown. Read parameters from LeakySingleton
      // and recreate the publisher thread again.
      worker_.wlock()->emplace();
    }
  }
};

namespace {
folly::Singleton<PublisherManager> publisherManager;
}

// ExportedStatMap will utilize a default stat object,
// MinuteTenMinuteHourTimeSeries, as a blueprint for creating new timeseries
// if one is not explicitly specified.  So we define these here that are used
// by their respective wrapper abstractions.
const ExportedStat& MinuteTimeseriesWrapper::templateExportedStat() {
  static const folly::Indestructible<MinuteTimeSeries<CounterType>> obj;
  return *obj.get();
}

const ExportedStat& QuarterMinuteOnlyTimeseriesWrapper::templateExportedStat() {
  static const folly::Indestructible<QuarterMinuteOnlyTimeSeries<CounterType>>
      obj;
  return *obj.get();
}

const ExportedStat& MinuteOnlyTimeseriesWrapper::templateExportedStat() {
  static const folly::Indestructible<MinuteOnlyTimeSeries<CounterType>> obj;
  return *obj.get();
}

ThreadCachedServiceData::StatsThreadLocal&
ThreadCachedServiceData::getStatsThreadLocal() {
  static folly::Indestructible<ThreadCachedServiceData::StatsThreadLocal>
      threadLocal{[]() {
        return new ThreadCachedServiceData::ThreadLocalStatsMap{fbData.ptr()};
      }};
  return *threadLocal;
}

ThreadCachedServiceData& ThreadCachedServiceData::getInternal() {
  static ThreadCachedServiceData* instance = new ThreadCachedServiceData();
  return *instance;
}

ThreadCachedServiceData* ThreadCachedServiceData::get() {
  publisherManager.vivify();
  return &getInternal();
}
std::shared_ptr<ThreadCachedServiceData> ThreadCachedServiceData::getShared() {
  return std::shared_ptr<ThreadCachedServiceData>(
      std::shared_ptr<void>{}, ThreadCachedServiceData::get());
}

ThreadCachedServiceData::ThreadCachedServiceData()
    : serviceData_{fbData.ptr()},
      threadLocalStats_{&ThreadCachedServiceData::getStatsThreadLocal()} {}

void ThreadCachedServiceData::publishStats() {
  auto start = std::chrono::steady_clock::now();
  uint64_t totalAggregateCalls = 0;
  uint64_t mapsAggregated = 0;
  for (ThreadLocalStatsMap& tlsm : threadLocalStats_->accessAllThreads()) {
    totalAggregateCalls += tlsm.aggregate();
    mapsAggregated++;
  }
  auto end = std::chrono::steady_clock::now();
  auto interval =
      std::chrono::duration_cast<std::chrono::microseconds>(end - start);
  STATS_fb303_tcData_publish_time_usec.add(interval.count());
  STATS_fb303_tcData_aggregate_call_count.add(totalAggregateCalls);
  STATS_fb303_tcData_tlmaps_aggregated.add(mapsAggregated);
}

void ThreadCachedServiceData::startPublishThread(milliseconds interval) {
  // startPublishThread(-1) is intended to be used by libraries that use
  // ThreadCachedServiceData and want to ensure that the publish thread are
  // running.  (Since they don't control the main function and therefore don't
  // know if it was started by the main program.)

  // If the interval isn't positive, perform a fast check to see if the thread
  // has already been started.
  if (interval <= milliseconds(0) &&
      interval_.load(std::memory_order_relaxed) != milliseconds(0)) {
    return;
  }

  if (interval <= milliseconds(0)) {
    // default to 1 second if the caller passed in a non-positive value.
    interval = milliseconds(1000);
  }

  if (auto mgr = publisherManager.try_get()) {
    mgr->worker_.withWLock([&](auto& worker) {
      interval_.store(interval, std::memory_order_relaxed);
      worker.emplace();
    });
  }
}

void ThreadCachedServiceData::stopPublishThread() {
  if (auto mgr = publisherManager.try_get()) {
    mgr->worker_.withWLock([&](auto& worker) {
      interval_.store(milliseconds(0), std::memory_order_relaxed);
      worker.reset();
    });
  }
}

bool ThreadCachedServiceData::publishThreadRunning() const {
  return getPublisherInterval() > milliseconds(0);
}

int64_t ThreadCachedServiceData::setCounter(
    folly::StringPiece key,
    int64_t value) {
  // Data set via setCounter() is implicitly not cachable in each thread:
  // the last call to setCounter() always has to win.  Therefore, set the
  // ServiceData counter directly rather than using the threadLocalStats_.
  //
  // In general, callers who use this API are not performance sensitive: almost
  // all of the call sites are callers who use it on rare occasions to reset
  // the counter to 0, or callers who maintain their own stat counter
  // internally and then periodically call setCounter() once every several
  // seconds to replace the exported ServiceData counter with their up-to-date
  // internal version.
  return getServiceData()->setCounter(key, value);
}

void ThreadCachedServiceData::clearCounter(folly::StringPiece key) {
  // As with setCounter(), clearCounter() is not easily cachable on a
  // per-thread basis, and is similarly non performance sensitive.
  getServiceData()->clearCounter(key);
}

void ThreadCachedServiceData::zeroStats() {
  // Call publishStats() to clear out all thread-cached data
  publishStats();
  // Then zero out the ServiceData stats
  getServiceData()->zeroStats();
}

void ThreadCachedServiceData::addHistAndStatValue(
    folly::StringPiece key,
    int64_t value,
    bool /*checkContains*/) {
  getThreadStats()->addStatValue(key, value);
  getThreadStats()->addHistogramValue(key, value);
}

void ThreadCachedServiceData::addHistAndStatValues(
    folly::StringPiece key,
    const folly::Histogram<int64_t>& values,
    time_t now,
    int64_t sum,
    int64_t nsamples,
    bool checkContains) {
  getServiceData()->addHistAndStatValues(
      key, values, now, sum, nsamples, checkContains);
}

void ThreadCachedServiceData::clearStat(
    folly::StringPiece key,
    ExportType exportType) {
  (*keyCacheTable_)[exportType].erase(key);
  ServiceData::get()->addStatExportType(key, exportType, nullptr);
}

void ThreadCachedServiceData::addStatValue(
    folly::StringPiece key,
    int64_t value,
    ExportType exportType) {
  if (UNLIKELY(!(*keyCacheTable_)[exportType].has(key))) {
    // This is not present in the threadlocal export set; possible it was
    // not yet registered with the underlying ServiceData impl.
    // This time around, pass it to the ServiceData so the type is exported
    getServiceData()->addStatExportType(key, exportType);
    (*keyCacheTable_)[exportType].add(key);
  }
  // now we know the export was done; finally bump the counter
  addStatValue(key, value);
}
} // namespace fb303
} // namespace facebook
