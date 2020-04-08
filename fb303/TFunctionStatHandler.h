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

#include <fb303/BaseService.h>
#include <folly/experimental/FunctionScheduler.h>
#include <thrift/lib/cpp/TProcessor.h>

namespace facebook {
namespace fb303 {

struct TStatsRequestContext {
  using clock = std::chrono::steady_clock;
  using time_point = clock::time_point;

  bool measureTime_ = false;
  bool readBeginCalled_ = false;
  bool readEndCalled_ = false;
  bool writeBeginCalled_ = false;
  bool writeEndCalled_ = false;
  bool exception = false;
  bool userException = false;
  bool async = false;
  uint32_t rBytes_ = 0;
  uint32_t wBytes_ = 0;
  // if measureTime, those will store timestamps of respective events
  time_point readBeginTime_{};
  time_point readEndTime_{};
  time_point writeBeginTime_{};
  time_point writeEndTime_{};

  void readBegin() {
    readBeginCalled_ = true;
    if (measureTime_) {
      readBeginTime_ = clock::now();
    }
  }

  void readEnd(uint32_t bytes) {
    readEndCalled_ = true;
    rBytes_ = bytes;
    if (measureTime_) {
      readEndTime_ = clock::now();
    }
  }

  void writeBegin() {
    writeBeginCalled_ = true;
    if (measureTime_) {
      writeBeginTime_ = clock::now();
    }
  }

  void writeEnd(uint32_t bytes) {
    writeEndCalled_ = true;
    wBytes_ = bytes;
    if (measureTime_) {
      writeEndTime_ = clock::now();
    }
  }

  void asyncComplete() {
    async = true;
  }

  void exceptionThrown() {
    exception = true;
  }

  void userExceptionThrown() {
    userException = true;
  }
};

/**
 * Class which holds sums, rates, counts, and times for a thrift function for
 * a single thread.  It's the smallest object of aggregation in this system.
 */
class TStatsPerThread {
 protected:
  virtual ~TStatsPerThread();

 public:
  /**
   * Get a Sampler object to return as the context for this TStatsPerThread.
   * Allocates a new Sampler if there are none left in the pool.
   */
  TStatsRequestContext* getContext();

  class StatsPerThreadHist {
   public:
    ~StatsPerThreadHist() = default;

    void setParameters(
        folly::small_vector<int> percentiles,
        CounterType bucketSize,
        CounterType min,
        CounterType max) {
      set(std::move(percentiles), bucketSize, min, max);
    }

    bool isEnabled() {
      return (exportedHist_ && hist_);
    }

    folly::small_vector<int>& getPercentiles() {
      return percentiles_;
    }

    ExportedHistogram* getExportedHistogram() {
      return exportedHist_.get();
    }

    folly::Histogram<CounterType>* getHistogram() {
      return hist_.get();
    }

   private:
    void set(
        folly::small_vector<int> percentiles,
        CounterType bucketSize,
        CounterType min,
        CounterType max);

    folly::small_vector<int> percentiles_;
    CounterType bucketSize_ = 0;
    CounterType min_ = 0;
    CounterType max_ = 0;

    std::unique_ptr<folly::Histogram<CounterType>> hist_;
    std::unique_ptr<ExportedHistogram> exportedHist_; // For consolidation
  };

  struct TimeSeries {
    uint32_t count = 0;
    uint64_t sum = 0;
    StatsPerThreadHist hist;

    void addValue(int64_t value) {
      count++;
      sum += value;
      auto h = hist.getHistogram();
      if (h) {
        h->addValue(value);
      }
    }

    void clear() {
      count = 0;
      sum = 0;
    }
  };

  /**
   * Enable histogram stats for read.
   */
  void enableThriftFuncHist(ThriftFuncHistParams* params);

  // add data from this request to stats, calling logContextDataProcessed,
  // which can be customized based on whether we observe thrift server or client
  void logContextData(const TStatsRequestContext& context);
  virtual void logContextDataProcessed(const TStatsRequestContext& context) = 0;

  void clear(); // clear all of object's sums & counts
  void setSampleRate(double rate); // set sampling fraction for timing

  std::mutex mutex_; // Mutex guarding threads stats collection
  uint32_t calls_; // total calls counted since last aggregation
  uint32_t processed_; // calls that finished processing
  uint32_t asyncs_; // total async calls counted
  uint32_t exceptions_; // total thrift undeclared exceptions counted
  uint32_t userExceptions_; // total thrift declared or undeclared exceptions
  TimeSeries readData_;
  TimeSeries writeData_;

  // timing data
  uint32_t samples_; // number of samples where timing was done
  TimeSeries readTime_;
  TimeSeries writeTime_;
  TimeSeries processTime_;
  TimeSeries totalCpuTime_;
  TimeSeries totalWorkedTime_;

  // fraction (<=1.0) of calls to be sampled for timing
  double sampleRate_ = 1.0;
  double sampleTimer_ = 0.0; // accumulates sample fractions

  // fraction of calls to be measured and logged for RequestStats
  double requestStatsMeasureRate_ = 0.0;
  double requestStatsLogRate_ = 0.0;
};

/**
 * Class which manages all function stats collection for a server.
 * To enable stats for a server, a TFunctionStatHandler should be
 * allocated,managed by a std::shared_ptr<TProcessorEventHandler>,
 * and installed in a server's TProcessor with setEventHandler().
 *
 * Only one TFunctionStatHandler should be created for a server, but
 * multiple servers within a given process space can be handled by
 * creating a TFunctionStatHandler for each.
 */
class TFunctionStatHandler
    : public apache::thrift::TProcessorEventHandler,
      public std::enable_shared_from_this<TFunctionStatHandler> {
  // we use folly::FunctionScheduler for periodic stats consolidation;
  folly::FunctionScheduler scheduler_;

  DynamicStrings dynamicStrings_; // holds the counters information
  ExportedHistogram dummyHist_;

  std::unordered_map<std::string, ThriftFuncHistParams> histParamsMap_;

  /**
   * Mapping from thrift functions to their respective
   * TStatsPerThread objects for a single thread
   */
  using TStatsAggregator =
      std::unordered_map<std::string, std::shared_ptr<TStatsPerThread>>;

  class Tag;
  folly::ThreadLocalPtr<TStatsAggregator, Tag> tlFunctionMap_;

  std::string getHistParamsMapKey(
      std::string funcName,
      ThriftFuncAction action);

 protected:
  std::recursive_mutex statMutex_; // mutex guarding thread-local function maps

  std::string counterNamePrefix_; // "thrift.service-name."
  std::string serviceName_;
  DynamicCounters* counters_;
  int32_t nThreads_; // active threads counted last period
  int32_t secondsPerPeriod_;
  double desiredSamplesPerPeriod_; // overall samples/period wanted
  fb303::ExportedStatMap statMapSum_; // sums/rates
  fb303::ExportedStatMap statMapAvg_; // averages
  ExportedHistogramMapImpl histogramMap_; // histogram

  static const std::string kDefaultCounterNamePrefix;

  /*
   * Work to be done after the construction of TFunctionStatHandler, from the
   * destructors of child classes. This starts the
   * FunctionScheduler thread to trigger periodic consolidation.
   */
  void postConstruct();

  /*
   * Work to be done before the destruction of TFunctionStatHandler, from the
   * destructors of child classes. This triggers last consolidation and
   * shuts off the FunctionScheduler.
   * This must be done before the destructor of TFunctionStatHandler to avoid
   * a race condition on consolidate call from the scheduler.
   */
  void preDestroy();

  /*
   * This function looks for a TStatsAggregator within the current thread's
   * thread-specific memory associated with key_, creating it if necessary.
   */
  TStatsPerThread* getStats(const std::string& fn_name);

  /**
   * Merge stats from a given thread
   */
  int32_t consolidateThread(time_t now, TStatsAggregator& functionMap);
  /**
   * Merge stats from the given TStatsPerThread object with existing counters
   */
  virtual int32_t
  consolidateStats(time_t now, const std::string& fnName, TStatsPerThread& spt);

 public:
  static const int32_t kSamplesPerSecond = 100; // default samples/second
  static const int32_t kSecondsPerPeriod = 5; // default period in seconds

  /**
   * Constructor for TFunctionStatHandler.  Initialize thread-specific
   * storage key, period timer, and hook up to dynamic counters.
   *
   * @param counters pointer to the server's dynamic counter object
   * @param sampPerSecond target # of timing samples per second
   * @param secondsPerPeriod sampling rate
   */
  TFunctionStatHandler(
      DynamicCounters* counters,
      const std::string& serviceName,
      int32_t sampPerSecond = kSamplesPerSecond,
      int32_t secondsPerPeriod = kSecondsPerPeriod,
      const std::string& counterNamePrefix = kDefaultCounterNamePrefix);

  /**
   * Construct an instance of TStatsPerThread.
   */
  virtual std::shared_ptr<TStatsPerThread> createStatsPerThread() = 0;

  /**
   * Calls setDefaultStat on all ExportedStatMapImpl members of this handler.
   * TODO(ryandm): Caveat, this function should be called before the first
   * aggregation timeout expires (~5 seconds from object creation). Task 950790.
   */
  void setDefaultStat(const ExportedStat& defaultStat) {
    statMapSum_.setDefaultStat(defaultStat);
    statMapAvg_.setDefaultStat(defaultStat);
  }

  /**
   * Aggregate the stats from all threads. Meant to be called periodically.
   */
  virtual void consolidate();

  /**
   * Get the stats-collection context for this call within the current thread.
   *
   * This obtains a thread-local TStatsAggregator and then a TStatsPerThread
   * object from that TStatsAggregator (which will create it if necessary) for
   * this function context.
   *
   * @returns current function & thread's new or existing TStatsPerThread,
   *          cast into a void*.
   */
  void* getContext(
      const char* fn_name,
      apache::thrift::server::TConnectionContext* /*serverContext*/ =
          nullptr) override {
    auto stats = getStats(std::string(fn_name));
    return (void*)(stats->getContext());
  }

  /**
   * Set histogram parameters for read/write/process in TStatsPerThread object.
   * This action enables histogram for the given function
   */
  void addThriftFuncHistParams(ThriftFuncHistParams params) {
    std::string key = getHistParamsMapKey(params.funcName, params.action);
    auto it = histParamsMap_.find(key);
    if (it == histParamsMap_.end()) {
      histParamsMap_.insert(std::make_pair(key, params));
    }
  }

  ThriftFuncHistParams* getThriftFuncHistParams(
      const char* fn_name,
      ThriftFuncAction action) {
    std::string key = getHistParamsMapKey(std::string(fn_name), action);
    auto it = histParamsMap_.find(key);
    if (it == histParamsMap_.end()) {
      return nullptr;
    }
    return &(it->second);
  }

  void setThriftHistParams(TStatsPerThread* stats, const char* fn_name);

  /**
   * Free resources associated with a context.
   */
  void freeContext(void* ctx, const char* fn_name) override;

  /**
   * Called before reading arguments.
   */
  void preRead(void* ctx, const char*) override;

  /**
   * Called between reading arguments and calling the handler.
   */
  void postRead(
      void* ctx,
      const char*,
      apache::thrift::transport::THeader*,
      uint32_t bytes) override;

  /**
   * Called between calling the handler and writing the response.
   */
  void preWrite(void* ctx, const char*) override;

  /**
   * Called after writing the response.
   */
  void postWrite(void* ctx, const char*, uint32_t bytes) override;

  /**
   * Called when an async function call completes successfully.
   */
  void asyncComplete(void* ctx, const char*) override;

  /**
   * Called if the handler throws an undeclared exception.
   */
  void handlerError(void* ctx, const char*) override;

  /**
   * Called if the handler throws any exception.
   */
  void userException(
      void* ctx,
      const char* fn_name,
      const std::string& ex,
      const std::string& ex_what) override;
};

void withThriftFunctionStats(
    const char* serviceName,
    BaseService* service,
    folly::Function<void()>&& fn);

} // namespace fb303
} // namespace facebook
