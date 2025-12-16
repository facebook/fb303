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

#include <fb303/ServiceData.h>
#include <fb303/TFunctionStatHandler.h>

#include <mutex>

#include <fb303/LegacyClock.h>

namespace {
int64_t count_usec(std::chrono::steady_clock::duration d) {
  return std::chrono::duration_cast<std::chrono::microseconds>(d).count();
}
} // namespace

namespace facebook::fb303 {

namespace {

constexpr ExportedStat::Duration kFiveSecondMinuteTenMinuteHourDurations[] = {
    std::chrono::seconds(5),
    std::chrono::seconds(60),
    std::chrono::seconds(600),
    std::chrono::seconds(3600),
    std::chrono::seconds(0)};

template <class T>
class FiveSecondMinuteTenMinuteHourTimeSeries : public MultiLevelTimeSeries<T> {
 public:
  enum Levels {
    FIVE_SECOND,
    MINUTE,
    TEN_MINUTE,
    HOUR,
    ALLTIME,
    NUM_LEVELS,
  };

  FiveSecondMinuteTenMinuteHourTimeSeries()
      : MultiLevelTimeSeries<T>(
            NUM_LEVELS,
            60,
            kFiveSecondMinuteTenMinuteHourDurations) {}
};

} // namespace

// Default key prefix for stats collected by TFunctionStatHandler
const std::string TFunctionStatHandler::kDefaultCounterNamePrefix("thrift.");

TStatsPerThread::~TStatsPerThread() = default;

TStatsRequestContext* TStatsPerThread::getContext() {
  auto context = new TStatsRequestContext();
  std::unique_lock lock(mutex_);
  // evaluate sampling
  sampleTimer_ += sampleRate_;
  if (sampleTimer_ >= 1.0) {
    sampleTimer_ -= 1.0;
    context->measureTime_ = true;
  }
  return context;
}

void TStatsPerThread::clear() {
  calls_ = 0;
  processed_ = 0;
  exceptions_ = 0;
  userExceptions_ = 0;
  readData_.clear();
  writeData_.clear();

  samples_ = 0;
  readTime_.clear();
  writeTime_.clear();
  processTime_.clear();
}

void TStatsPerThread::setSampleRate(double rate) {
  if (rate > 1.0 || rate < 0.0) {
    rate = 1.0;
  }
  sampleRate_ = rate;
}

void TStatsPerThread::setQuantileStats(SharedQuantileStats& stats) {
  processTime_.quantileStat = stats.processTime_;
}

void TStatsPerThread::logContextData(const TStatsRequestContext& context) {
  std::unique_lock lock(mutex_);
  calls_++;
  samples_ += context.measureTime_;
  exceptions_ += context.exception;
  userExceptions_ += context.userException;
  if (context.readEndCalled_) {
    CHECK(context.readBeginCalled_);
    readData_.addValue(context.rBytes_);
    if (context.measureTime_) {
      readTime_.addValue(
          count_usec(context.readEndTime_ - context.readBeginTime_));
    }
  }
  if (context.writeEndCalled_) {
    CHECK(context.writeBeginCalled_);
    writeData_.addValue(context.wBytes_);
    if (context.measureTime_) {
      writeTime_.addValue(
          count_usec(context.writeEndTime_ - context.writeBeginTime_));
    }
  }
  logContextDataProcessed(context);
}

TFunctionStatHandler::TFunctionStatHandler(
    DynamicCounters* counters,
    const std::string& serviceName,
    int32_t sampPerSecond,
    int32_t secondsPerPeriod,
    const std::string& counterNamePrefix,
    bool useSubMinuteIntervalCounters)
    : counterNamePrefix_(counterNamePrefix),
      serviceName_(serviceName),
      counters_(counters),
      nThreads_(1),
      secondsPerPeriod_(secondsPerPeriod),
      desiredSamplesPerPeriod_(secondsPerPeriod * sampPerSecond),
      statMapSum_(
          counters_,
          {SUM, RATE},
          useSubMinuteIntervalCounters
              ? ExportedStat(FiveSecondMinuteTenMinuteHourTimeSeries<int64_t>())
              : ExportedStat(MinuteTenMinuteHourTimeSeries<int64_t>())),
      statMapAvg_(
          counters_,
          AVG,
          useSubMinuteIntervalCounters
              ? ExportedStat(FiveSecondMinuteTenMinuteHourTimeSeries<int64_t>())
              : ExportedStat(MinuteTenMinuteHourTimeSeries<int64_t>())) {
  assert(desiredSamplesPerPeriod_ > 0);
}

void TFunctionStatHandler::freeContext(void* ctx, std::string_view fn_name) {
  if (ctx != nullptr) {
    auto context = static_cast<TStatsRequestContext*>(ctx);
    getStats(fn_name)->logContextData(*context);
    delete context;
  }
}

void TFunctionStatHandler::preRead(void* ctx, std::string_view) {
  if (ctx != nullptr) {
    static_cast<TStatsRequestContext*>(ctx)->readBegin();
  }
}

void TFunctionStatHandler::postRead(
    void* ctx,
    std::string_view,
    apache::thrift::transport::THeader*,
    uint32_t bytes) {
  if (ctx != nullptr) {
    static_cast<TStatsRequestContext*>(ctx)->readEnd(bytes);
  }
}

void TFunctionStatHandler::preWrite(void* ctx, std::string_view) {
  if (ctx != nullptr) {
    static_cast<TStatsRequestContext*>(ctx)->writeBegin();
  }
}

void TFunctionStatHandler::postWrite(
    void* ctx,
    std::string_view,
    uint32_t bytes) {
  if (ctx != nullptr) {
    static_cast<TStatsRequestContext*>(ctx)->writeEnd(bytes);
  }
}

void TFunctionStatHandler::handlerError(void* ctx, std::string_view) {
  if (ctx != nullptr) {
    static_cast<TStatsRequestContext*>(ctx)->exceptionThrown();
  }
}

void TFunctionStatHandler::userExceptionWrapped(
    void* ctx,
    std::string_view /*fn_name*/,
    bool /*declared*/,
    const folly::exception_wrapper& /*ew_*/) {
  if (ctx != nullptr) {
    static_cast<TStatsRequestContext*>(ctx)->userExceptionThrown();
  }
}

SharedQuantileStats TFunctionStatHandler::getSharedQuantileStats(
    std::string_view fnName) {
  SharedQuantileStats quantileStats;
  quantileStats.processTime_ = fbData->getQuantileStat(
      fmt::format("{}{}.time_process_us", counterNamePrefix_, fnName),
      // No need to export anything here, sum / count / average are handled +
      // aggregated by per thread stats.
      ExportTypeConsts::kNone,
      QuantileConsts::kP10_P50_P90_P95_P99_P100,
      SlidingWindowPeriodConsts::kOneMin);
  return quantileStats;
}

void TFunctionStatHandler::postConstruct() {
  using namespace std::chrono;
  scheduler_.addFunction(
      [this] { this->consolidate(); },
      duration_cast<milliseconds>(seconds(secondsPerPeriod_)),
      "fb303-consolidate");
  scheduler_.setThreadName("fb303-consolidate");
  auto started = scheduler_.start();
  DCHECK(started);
}

void TFunctionStatHandler::preDestroy() {
  VLOG(2) << "Shutting scheduler thread down...";
  auto shutdown = scheduler_.shutdown();
  DCHECK(shutdown);

  consolidate();
  VLOG(2) << "Cleanup finished!";
}

void TFunctionStatHandler::consolidate() {
  std::unique_lock lock(statMutex_);
  auto now = get_legacy_stats_time();

  // call consolidateStats for all threads and all functions
  auto threadCounter = 0;
  for (auto& map : tlFunctionMap_.accessAllThreads()) {
    auto calls = consolidateThread(now, map);
    // increment threadCounter if the thread accepted at least 1 request
    threadCounter += calls > 0;
  }
  // Update thread counts based on result of loop just executed
  if (threadCounter > 0) {
    nThreads_ = threadCounter;
  }
}

int32_t TFunctionStatHandler::consolidateThread(
    time_t now,
    TStatsAggregator& functionMap) {
  auto calls = 0;
  for (auto& stats : functionMap) {
    if (!stats.second) {
      continue;
    }
    calls += consolidateStats(now, stats.first, *stats.second);
  }
  return calls;
}

int32_t TFunctionStatHandler::consolidateStats(
    time_t nowSec,
    const std::string& fnName,
    TStatsPerThread& spt) {
  std::unique_lock lock(spt.mutex_);

  auto calls = spt.calls_;
  TimePoint now{std::chrono::seconds(nowSec)};

  // Note that in this section all the counters are
  // per method - not aggregated across all the methods of the service
  auto addAllValues = [&](const auto& prefix) {
    // update counts

    // number of calls that are made
    statMapSum_.addValue(prefix + ".num_calls", now, spt.calls_);
    // called hook is here - https://fburl.com/code/5ztnw92a (postRead(2))
    // number of calls to read
    // read means reads from request channel (deserialization)
    statMapSum_.addValue(prefix + ".num_reads", now, spt.readData_.count);
    // called hook is here - https://fburl.com/code/f19kbzg5 (postWrite(1))
    // number of calls to write
    // write means writes to response channel (serialization)
    statMapSum_.addValue(prefix + ".num_writes", now, spt.writeData_.count);
    // number of calls that actually got processed
    statMapSum_.addValue(prefix + ".num_processed", now, spt.processed_);
    // userExceptions is the Thrift name for all exceptions escaped from the
    // handler counter is named differently to better represent what it
    // actually means
    statMapSum_.addValue(
        prefix + ".num_all_exceptions", now, spt.userExceptions_);
    // this counter only includes exceptions not declared in the Thrift schema
    statMapSum_.addValue(prefix + ".num_exceptions", now, spt.exceptions_);
    // number of samples collected
    statMapSum_.addValue(prefix + ".num_samples", now, spt.samples_);
    // number of bytes read from request channel (deserialization)
    statMapSum_.addValue(prefix + ".bytes_read", now, spt.readData_.sum);
    // number of bytes written to response channel (serialization)
    statMapSum_.addValue(prefix + ".bytes_written", now, spt.writeData_.sum);

    if (spt.requestStatsMeasureRate_ > 1e-9) {
      statMapAvg_.addValue(
          prefix + ".request_stats_rate",
          now,
          1 / spt.requestStatsMeasureRate_);
    }
    if (spt.requestStatsLogRate_ > 1e-9) {
      statMapAvg_.addValue(
          prefix + ".request_stats_log_rate",
          now,
          1 / spt.requestStatsLogRate_);
    }

    // update averages

    statMapAvg_.addValueAggregated(
        prefix + ".bytes_read", now, spt.readData_.sum, spt.readData_.count);
    statMapAvg_.addValueAggregated(
        prefix + ".bytes_written",
        now,
        spt.writeData_.sum,
        spt.writeData_.count);
    // same hook as .num_reads
    // while recording time spent
    statMapAvg_.addValueAggregated(
        prefix + ".time_read_us", now, spt.readTime_.sum, spt.readTime_.count);
    // same hook as .num_writes
    // while recording time spent
    statMapAvg_.addValueAggregated(
        prefix + ".time_write_us",
        now,
        spt.writeTime_.sum,
        spt.writeTime_.count);

    // Recording the time from when the request is read from the socket
    // (https://fburl.com/code/xjb7fgyn)
    // to when its response is ready to be written back
    // (https://fburl.com/code/saisy2wd)
    // This is solely dependent on request lifecycle, and it the request
    // is never completed, this counter wouldn't be updated
    statMapAvg_.addValueAggregated(
        prefix + ".time_process_us",
        now,
        spt.processTime_.sum,
        spt.processTime_.count);

    // Recording the time for the request to be totally on cpu
    // (https://fburl.com/code/jhpal24s)
    statMapAvg_.addValueAggregated(
        prefix + ".total_cpu_us",
        now,
        spt.totalCpuTime_.sum,
        spt.totalCpuTime_.count);

    // Recording the time for the request to be running on CPU
    // thread (https://fburl.com/code/d1m14dvg)
    statMapAvg_.addValueAggregated(
        prefix + ".total_worked_us",
        now,
        spt.totalWorkedTime_.sum,
        spt.totalWorkedTime_.count);
  };

  addAllValues(counterNamePrefix_ + fnName);

  // update sample rate
  if (spt.calls_ > 0) {
    spt.setSampleRate(desiredSamplesPerPeriod_ / nThreads_ / spt.calls_);
  } else {
    spt.setSampleRate(1.0);
  }

  // zero out stats for new period
  spt.clear();
  return calls;
}

TStatsPerThread* TFunctionStatHandler::getStats(std::string_view fnName) {
  auto mapPtr = tlFunctionMap_.get();
  if (mapPtr == nullptr) {
    mapPtr = new TStatsAggregator();
    std::weak_ptr<TFunctionStatHandler> wp(shared_from_this());
    auto deleter = [wp](TStatsAggregator* a, folly::TLPDestructionMode mode) {
      if (mode == folly::TLPDestructionMode::THIS_THREAD) {
        auto sp = wp.lock();
        if (sp) {
          std::unique_lock lock(sp->statMutex_);
          sp->consolidateThread(get_legacy_stats_time(), *a);
        }
      }
      delete a;
    };
    std::unique_lock lock(statMutex_);
    tlFunctionMap_.reset(mapPtr, deleter);
  }
  // Find TStatsPerThread in TStatsAggregator's map - the map is only updated
  // from one thread (the current one, owner of the TStatsAggregator); no
  // update should be needed in the common case, so we just use statMutex_
  // to guard it
  auto& map = *mapPtr;
  auto it = map.find(fnName);
  if (it == map.end()) {
    auto stats = createStatsPerThread(fnName);
    auto sharedQuantileStats = getSharedQuantileStats(fnName);
    stats->setQuantileStats(sharedQuantileStats);

    // we're going to be writing the map, so lock out stat aggregation ftm
    std::unique_lock lock(statMutex_);
    map[fnName] = stats;
    return stats.get();
  }
  return it->second.get();
}

namespace {

class StandardStatsPerThread : public TStatsPerThread {
  void logContextDataProcessed(const TStatsRequestContext& context) override {
    // for thrift servers, reads happen first, then writes
    if (!context.writeBeginCalled_) {
      return;
    }
    CHECK(context.readEndCalled_);
    processed_++;
    if (context.measureTime_) {
      processTime_.addValue(
          count_usec(context.writeBeginTime_ - context.readEndTime_));
    }
  }
};

class StandardStatHandler : public TFunctionStatHandler {
 public:
  StandardStatHandler(const char* serviceName)
      : TFunctionStatHandler{fbData->getDynamicCounters(), serviceName} {
    postConstruct();
  }

  StandardStatHandler(const StandardStatHandler&) = delete;
  StandardStatHandler& operator=(const StandardStatHandler&) = delete;
  StandardStatHandler(StandardStatHandler&&) = delete;
  StandardStatHandler& operator=(StandardStatHandler&&) = delete;

  ~StandardStatHandler() {
    preDestroy();
  }

  std::shared_ptr<TStatsPerThread> createStatsPerThread(
      std::string_view) override {
    return std::make_shared<StandardStatsPerThread>();
  }
};

} // namespace

std::shared_ptr<TFunctionStatHandler> addThriftFunctionStatHandler(
    const char* serviceName) {
  auto handler = std::make_shared<StandardStatHandler>(serviceName);
  apache::thrift::TProcessorBase::addProcessorEventHandler_deprecated(handler);
  return handler;
}

void withThriftFunctionStats(
    const char* serviceName,
    BaseService* service,
    folly::Function<void()>&& fn) {
  auto handler = std::make_shared<StandardStatHandler>(serviceName);

  apache::thrift::TProcessorBase::addProcessorEventHandler_deprecated(handler);
  SCOPE_EXIT {
    apache::thrift::TProcessorBase::removeProcessorEventHandler(handler);
  };

  fn();
}

} // namespace facebook::fb303
