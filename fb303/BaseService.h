/*
 * Copyright 2019-present Facebook, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
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

namespace facebook {
namespace fb303 {

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
  void getCounters(std::map<std::string, int64_t>& _return) override {
    ServiceData::get()->getCounters(_return);
  }

  /*** Retrieves all counters that match a regex */
  void getRegexCounters(
      std::map<std::string, int64_t>& _return,
      std::unique_ptr<std::string> regex) override {
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
    } catch (const std::invalid_argument& ex) {
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

 private:
  const std::string name_;
};

} // namespace fb303
} // namespace facebook
