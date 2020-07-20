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

#include <fb303/ServiceData.h>
#include <fb303/thrift/gen-cpp2/BaseService.h>
#include <folly/executors/CPUThreadPoolExecutor.h>
#include <folly/small_vector.h>

namespace facebook {
namespace fb303 {

enum ThriftFuncAction {
  FIRST_ACTION = 0,
  READ = FIRST_ACTION,
  WRITE,
  PROCESS,
  BYTES_READ,
  BYTES_WRITTEN,
  LAST_ACTION
};

struct ThriftFuncHistParams {
  explicit ThriftFuncHistParams(
      const std::string& funcName_,
      ThriftFuncAction action_,
      folly::small_vector<int> percentiles_,
      int64_t bucketSize_,
      int64_t min_,
      int64_t max_)
      : funcName(funcName_),
        action(action_),
        percentiles(std::move(percentiles_)),
        bucketSize(bucketSize_),
        min(min_),
        max(max_) {}

  std::string funcName;
  ThriftFuncAction action;
  folly::small_vector<int> percentiles;
  int64_t bucketSize;
  int64_t min;
  int64_t max;
};

class BaseService : virtual public cpp2::BaseServiceSvIf {
 protected:
  explicit BaseService(std::string name) : name_{std::move(name)} {}
  ~BaseService() override;

 public:
  void getName(std::string& _return) override {
    _return = name_;
  }

  /**
   * Local call to get the service name (since we need it to instantiate a
   * call stats handler and setting up a cob to get it would be silly).
   *
   * @return the service name.
   */
  const std::string& getName() const {
    return name_;
  }

  void getVersion(std::string& _return) override {
    _return = "";
  }

  void getStatusDetails(std::string& _return) override {
    _return = "";
  }

  /*** Retrieves all counters, both regular-style and dynamic counters */
  virtual void getCounters(std::map<std::string, int64_t>& _return) {
    ServiceData::get()->getCounters(_return);
  }

  /*** Retrieves all counters that match a regex */
  virtual void getRegexCounters(
      std::map<std::string, int64_t>& _return,
      std::unique_ptr<std::string> regex) {
    ServiceData::get()->getRegexCounters(_return, *regex);
  }

  /*** Returns a list of counter values */
  void getSelectedCounters(
      std::map<std::string, int64_t>& _return,
      std::unique_ptr<std::vector<std::string>> keys) override {
    ServiceData::get()->getSelectedCounters(_return, *keys);
  }

  /*** Retrieves a counter value for given key (could be regular or dynamic) */
  int64_t getCounter(std::unique_ptr<std::string> key) override {
    try {
      return ServiceData::get()->getCounter(*key);
    } catch (const std::invalid_argument&) {
      return 0;
    }
  }

  void getExportedValues(std::map<std::string, std::string>& _return) override {
    ServiceData::get()->getExportedValues(_return);
  }

  void getSelectedExportedValues(
      std::map<std::string, std::string>& _return,
      std::unique_ptr<std::vector<std::string>> keys) override {
    ServiceData::get()->getSelectedExportedValues(_return, *keys);
  }

  void getRegexExportedValues(
      std::map<std::string, std::string>& _return,
      std::unique_ptr<std::string> regex) override {
    ServiceData::get()->getRegexExportedValues(_return, *regex);
  }

  void getExportedValue(std::string& _return, std::unique_ptr<std::string> key)
      override {
    ServiceData::get()->getExportedValue(_return, *key);
  }

  void setOption(
      std::unique_ptr<std::string> key,
      std::unique_ptr<std::string> value) override {
    ServiceData::get()->setOption(*key, *value);
  }

  void getOption(std::string& _return, std::unique_ptr<std::string> key)
      override {
    try {
      _return = ServiceData::get()->getOption(*key);
    } catch (const std::invalid_argument&) {
      _return = "";
    }
  }

  void getOptions(std::map<std::string, std::string>& _return) override {
    ServiceData::get()->getOptions(_return);
  }

  int64_t aliveSince() override {
    return ServiceData::get()->getAliveSince().count();
  }

  /**
   * Add an automatically sampled and consolidated  histogram stat for a
   * thrift function. It adds histogram stats like
   * thrift.SERVICE.FUNCTION.PXX.INTERVAL to fb303.
   *
   * @param funcName    full function name, like SERVICE.FUNCTION
   * @param action      time for READ/WRITE/PROCESS
   * @param percentiles  define pxx
   * @param bucketSize  size of each bucket
   * @param min         min value of the histogram
   * @param max         max value of the histogram
   */
  void exportThriftFuncHist(
      const std::string& funcName,
      ThriftFuncAction action,
      folly::small_vector<int> percentiles,
      int64_t bucketSize,
      int64_t min,
      int64_t max) {
    thriftFuncHistParams_.emplace_back(
        funcName, action, percentiles, bucketSize, min, max);
  }

  void exportThriftFuncHist(
      const std::string& funcName,
      ThriftFuncAction action,
      int percentile,
      int64_t bucketSize,
      int64_t min,
      int64_t max) {
    exportThriftFuncHist(
        funcName,
        action,
        folly::small_vector<int>({percentile}),
        bucketSize,
        min,
        max);
  }

  const std::vector<ThriftFuncHistParams>& getExportedThriftFuncHist() const {
    return thriftFuncHistParams_;
  }

  /**
   * getCounters() is processed in event base so that it won't be blocked by
   * unhealthy cpu thread pool. We also don't want to mark as high priority
   * because it's more time consuming than getStatus().
   */
  void async_eb_getCounters(
      std::unique_ptr<apache::thrift::HandlerCallback<
          std::unique_ptr<std::map<std::string, int64_t>>>> callback) override {
    getCountersExecutor_.add(
        [this,
         callback_ = std::move(callback),
         keepAlive = folly::getKeepAliveToken(getCountersExecutor_)]() {
          try {
            std::map<std::string, int64_t> res;
            getCounters(res);
            callback_->result(res);
          } catch (...) {
            callback_->exception(std::current_exception());
          }
        });
  }

  void async_eb_getRegexCounters(
      std::unique_ptr<apache::thrift::HandlerCallback<
          std::unique_ptr<std::map<std::string, int64_t>>>> callback,
      std::unique_ptr<std::string> regex) override {
    getCountersExecutor_.add(
        [this,
         callback_ = std::move(callback),
         regex_ = std::move(regex),
         keepAlive = folly::getKeepAliveToken(getCountersExecutor_)]() mutable {
          try {
            std::map<std::string, int64_t> res;
            getRegexCounters(res, std::move(regex_));
            callback_->result(res);
          } catch (...) {
            callback_->exception(std::current_exception());
          }
        });
  }

 private:
  const std::string name_;
  std::vector<ThriftFuncHistParams> thriftFuncHistParams_;
  folly::CPUThreadPoolExecutor getCountersExecutor_{
      2,
      std::make_shared<folly::NamedThreadFactory>("GetCountersCPU")};
};

} // namespace fb303
} // namespace facebook
