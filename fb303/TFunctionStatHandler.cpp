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

#include <fb303/TFunctionStatHandler.h>
#include <fb303/LegacyClock.h>

using std::chrono::steady_clock;

namespace {
int64_t count_usec(std::chrono::steady_clock::duration d) {
  return std::chrono::duration_cast<std::chrono::microseconds>(d).count();
}
} // namespace

namespace facebook {
namespace fb303 {

// Default key prefix for stats collected by TFunctionStatHandler
const std::string TFunctionStatHandler::kDefaultCounterNamePrefix("thrift.");

TStatsPerThread::~TStatsPerThread() = default;

TStatsRequestContext* TStatsPerThread::getContext() {
  auto context = new TStatsRequestContext();
  std::lock_guard<std::mutex> lock(mutex_);
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
  asyncs_ = 0;
  exceptions_ = 0;
  userExceptions_ = 0;
  readData_.clear();
  writeData_.clear();

  samples_ = 0;
  readTime_.clear();
  writeTime_.clear();
  processTime_.clear();
}

void TStatsPerThread::enableThriftFuncHist(ThriftFuncHistParams* params) {
  if (params == nullptr) {
    return;
  }

  switch (params->action) {
    case ThriftFuncAction::READ:
      readTime_.hist.setParameters(
          params->percentiles, params->bucketSize, params->min, params->max);
      break;
    case ThriftFuncAction::WRITE:
      writeTime_.hist.setParameters(
          params->percentiles, params->bucketSize, params->min, params->max);
      break;
    case ThriftFuncAction::PROCESS:
      processTime_.hist.setParameters(
          params->percentiles, params->bucketSize, params->min, params->max);
      break;
    case ThriftFuncAction::BYTES_READ:
      readData_.hist.setParameters(
          params->percentiles, params->bucketSize, params->min, params->max);
      break;
    case ThriftFuncAction::BYTES_WRITTEN:
      writeData_.hist.setParameters(
          params->percentiles, params->bucketSize, params->min, params->max);
      break;
    default:
      break;
  }
}

void TStatsPerThread::setSampleRate(double rate) {
  if (rate > 1.0 || rate < 0.0) {
    rate = 1.0;
  }
  sampleRate_ = rate;
}

void TStatsPerThread::StatsPerThreadHist::set(
    folly::small_vector<int> percentiles,
    CounterType bucketSize,
    CounterType min,
    CounterType max) {
  // Allocate these first to make this function exception-atomic.
  auto exportedHist = std::make_unique<ExportedHistogram>(bucketSize, min, max);
  exportedHist->clear();
  auto hist =
      std::make_unique<folly::Histogram<CounterType>>(bucketSize, min, max);
  hist->clear();

  // Everything beloew is noexcept.
  percentiles_ = std::move(percentiles);
  bucketSize_ = bucketSize;
  min_ = min;
  max_ = max;
  exportedHist_ = std::move(exportedHist);
  hist_ = std::move(hist);
}

void TStatsPerThread::logContextData(const TStatsRequestContext& context) {
  std::lock_guard<std::mutex> lock(mutex_);
  calls_++;
  samples_ += context.measureTime_;
  exceptions_ += context.exception;
  userExceptions_ += context.userException;
  asyncs_ += context.async;
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

void TFunctionStatHandler::setThriftHistParams(
    TStatsPerThread* stats,
    const char* fn_name) {
  ThriftFuncHistParams* params = nullptr;
  for (int action = ThriftFuncAction::FIRST_ACTION;
       action != ThriftFuncAction::LAST_ACTION;
       action++) {
    params = getThriftFuncHistParams(fn_name, ThriftFuncAction(action));
    if (params) {
      stats->enableThriftFuncHist(params);
    }
  }
}

TFunctionStatHandler::TFunctionStatHandler(
    DynamicCounters* counters,
    const std::string& serviceName,
    int32_t sampPerSecond,
    int32_t secondsPerPeriod,
    const std::string& counterNamePrefix)
    : dummyHist_(1, 0, 1),
      counterNamePrefix_(counterNamePrefix),
      serviceName_(serviceName),
      counters_(counters),
      nThreads_(1),
      secondsPerPeriod_(secondsPerPeriod),
      desiredSamplesPerPeriod_(secondsPerPeriod * sampPerSecond),
      statMapSum_(counters_, {SUM, RATE}),
      statMapAvg_(counters_, AVG),
      histogramMap_(counters_, &dynamicStrings_, dummyHist_) {
  assert(desiredSamplesPerPeriod_ > 0);
}

void TFunctionStatHandler::freeContext(void* ctx, const char* fn_name) {
  if (ctx != nullptr) {
    auto context = static_cast<TStatsRequestContext*>(ctx);
    getStats(fn_name)->logContextData(*context);
    delete context;
  }
}

void TFunctionStatHandler::preRead(void* ctx, const char*) {
  if (ctx != nullptr) {
    static_cast<TStatsRequestContext*>(ctx)->readBegin();
  }
}

void TFunctionStatHandler::postRead(
    void* ctx,
    const char*,
    apache::thrift::transport::THeader*,
    uint32_t bytes) {
  if (ctx != nullptr) {
    static_cast<TStatsRequestContext*>(ctx)->readEnd(bytes);
  }
}

void TFunctionStatHandler::preWrite(void* ctx, const char*) {
  if (ctx != nullptr) {
    static_cast<TStatsRequestContext*>(ctx)->writeBegin();
  }
}

void TFunctionStatHandler::postWrite(void* ctx, const char*, uint32_t bytes) {
  if (ctx != nullptr) {
    static_cast<TStatsRequestContext*>(ctx)->writeEnd(bytes);
  }
}

void TFunctionStatHandler::asyncComplete(void* ctx, const char*) {
  if (ctx != nullptr) {
    static_cast<TStatsRequestContext*>(ctx)->asyncComplete();
  }
}

void TFunctionStatHandler::handlerError(void* ctx, const char*) {
  if (ctx != nullptr) {
    static_cast<TStatsRequestContext*>(ctx)->exceptionThrown();
  }
}

void TFunctionStatHandler::userException(
    void* ctx,
    const char* /* fn_name */,
    const std::string& /* ex */,
    const std::string& /* ex_what */) {
  if (ctx != nullptr) {
    static_cast<TStatsRequestContext*>(ctx)->userExceptionThrown();
  }
}

void TFunctionStatHandler::postConstruct() {
  using namespace std::chrono;
  scheduler_.addFunction(
      [this] { this->consolidate(); },
      duration_cast<milliseconds>(seconds(secondsPerPeriod_)));
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
  std::lock_guard<std::recursive_mutex> lock(statMutex_);
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
    time_t now,
    const std::string& fnName,
    TStatsPerThread& spt) {
  std::lock_guard<std::mutex> lock(spt.mutex_);

  auto calls = spt.calls_;
  auto addAllValues = [&](const auto& prefix) {
    // update counts
    statMapSum_.addValue(prefix + ".num_calls", now, spt.calls_);
    statMapSum_.addValue(prefix + ".num_reads", now, spt.readData_.count);
    statMapSum_.addValue(prefix + ".num_writes", now, spt.writeData_.count);
    statMapSum_.addValue(prefix + ".num_processed", now, spt.processed_);
    statMapSum_.addValue(prefix + ".num_asyncs", now, spt.asyncs_);
    // userExceptions is Thrift name for all exceptions escaped from the handler
    // counter is named differently to better represent what it actually means
    statMapSum_.addValue(
        prefix + ".num_all_exceptions", now, spt.userExceptions_);
    // this counter only includes exceptions not declared in the Thrift schema
    statMapSum_.addValue(prefix + ".num_exceptions", now, spt.exceptions_);
    statMapSum_.addValue(prefix + ".num_samples", now, spt.samples_);
    statMapSum_.addValue(prefix + ".bytes_read", now, spt.readData_.sum);
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
    statMapAvg_.addValueAggregated(
        prefix + ".time_read_us", now, spt.readTime_.sum, spt.readTime_.count);
    statMapAvg_.addValueAggregated(
        prefix + ".time_write_us",
        now,
        spt.writeTime_.sum,
        spt.writeTime_.count);
    statMapAvg_.addValueAggregated(
        prefix + ".time_process_us",
        now,
        spt.processTime_.sum,
        spt.processTime_.count);
    statMapAvg_.addValueAggregated(
        prefix + ".total_cpu_us",
        now,
        spt.totalCpuTime_.sum,
        spt.totalCpuTime_.count);
    statMapAvg_.addValueAggregated(
        prefix + ".total_worked_us",
        now,
        spt.totalWorkedTime_.sum,
        spt.totalWorkedTime_.count);

    // update histogram
    if (spt.readTime_.hist.isEnabled()) {
      histogramMap_.addValues(
          prefix + ".time_read_us",
          now,
          *spt.readTime_.hist.getHistogram(),
          spt.readTime_.hist.getExportedHistogram(),
          spt.readTime_.hist.getPercentiles());
    }

    if (spt.writeTime_.hist.isEnabled()) {
      histogramMap_.addValues(
          prefix + ".time_write_us",
          now,
          *spt.writeTime_.hist.getHistogram(),
          spt.writeTime_.hist.getExportedHistogram(),
          spt.writeTime_.hist.getPercentiles());
    }

    if (spt.processTime_.hist.isEnabled()) {
      histogramMap_.addValues(
          prefix + ".time_process_us",
          now,
          *spt.processTime_.hist.getHistogram(),
          spt.processTime_.hist.getExportedHistogram(),
          spt.processTime_.hist.getPercentiles());
    }
    if (spt.readData_.hist.isEnabled()) {
      histogramMap_.addValues(
          prefix + ".bytes_read",
          now,
          *spt.readData_.hist.getHistogram(),
          spt.readData_.hist.getExportedHistogram(),
          spt.readData_.hist.getPercentiles());
    }
    if (spt.writeData_.hist.isEnabled()) {
      histogramMap_.addValues(
          prefix + ".bytes_written",
          now,
          *spt.writeData_.hist.getHistogram(),
          spt.writeData_.hist.getExportedHistogram(),
          spt.writeData_.hist.getPercentiles());
    }
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

std::string TFunctionStatHandler::getHistParamsMapKey(
    std::string funcName,
    ThriftFuncAction action) {
  std::string key = funcName;
  switch (action) {
    case ThriftFuncAction::READ:
      key += ".READ";
      break;
    case ThriftFuncAction::WRITE:
      key += ".WRITE";
      break;
    case ThriftFuncAction::PROCESS:
      key += ".PROCESS";
      break;
    case ThriftFuncAction::BYTES_READ:
      key += ".BYTES_READ";
      break;
    case ThriftFuncAction::BYTES_WRITTEN:
      key += ".BYTES_WRITTEN";
      break;
    default:
      key += ".INVALID";
      break;
  }
  return key;
}

TStatsPerThread* TFunctionStatHandler::getStats(const std::string& fn_name) {
  auto mapPtr = tlFunctionMap_.get();
  if (mapPtr == nullptr) {
    mapPtr = new TStatsAggregator();
    std::weak_ptr<TFunctionStatHandler> wp(shared_from_this());
    auto deleter = [wp](TStatsAggregator* a, folly::TLPDestructionMode mode) {
      if (mode == folly::TLPDestructionMode::THIS_THREAD) {
        auto sp = wp.lock();
        if (sp) {
          std::lock_guard<std::recursive_mutex> lock(sp->statMutex_);
          sp->consolidateThread(get_legacy_stats_time(), *a);
        }
      }
      delete a;
    };
    std::lock_guard<std::recursive_mutex> lock(statMutex_);
    tlFunctionMap_.reset(mapPtr, deleter);
  }
  // Find TStatsPerThread in TStatsAggregator's map - the map is only updated
  // from one thread (the current one, owner of the TStatsAggregator); no
  // update should be needed in the common case, so we just use statMutex_
  // to guard it
  auto& map = *mapPtr;
  auto it = map.find(fn_name);
  if (it == map.end()) {
    // we're going to be writing the map, so lock out stat aggregation ftm
    std::lock_guard<std::recursive_mutex> lock(statMutex_);
    auto stats = createStatsPerThread();
    setThriftHistParams(stats.get(), fn_name.c_str());
    map[fn_name] = stats;
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

  ~StandardStatHandler() {
    preDestroy();
  }

  std::shared_ptr<TStatsPerThread> createStatsPerThread() override {
    return std::make_shared<StandardStatsPerThread>();
  }
};

template <class TStatHandler_>
class TThriftStatsHandlerFactory
    : public apache::thrift::TProcessorEventHandlerFactory {
 public:
  explicit TThriftStatsHandlerFactory(std::shared_ptr<TStatHandler_> handler)
      : handler_{std::move(handler)} {}

  std::shared_ptr<apache::thrift::TProcessorEventHandler> getEventHandler()
      override {
    return handler_;
  }

 private:
  std::shared_ptr<TStatHandler_> handler_;
};

} // namespace

void withThriftFunctionStats(
    const char* serviceName,
    BaseService* service,
    folly::Function<void()>&& fn) {
  auto handler = std::make_shared<StandardStatHandler>(serviceName);
  for (auto& thriftFuncHistParams : service->getExportedThriftFuncHist()) {
    handler->addThriftFuncHistParams(thriftFuncHistParams);
  }

  auto factory =
      std::make_shared<TThriftStatsHandlerFactory<StandardStatHandler>>(
          handler);

  apache::thrift::TProcessorBase::addProcessorEventHandlerFactory(factory);
  SCOPE_EXIT {
    apache::thrift::TProcessorBase::removeProcessorEventHandlerFactory(factory);
  };

  fn();
}

} // namespace fb303
} // namespace facebook
