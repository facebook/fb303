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

#include <fb303/FollyLoggingHandler.h>

#include <fb303/ServiceData.h>
#include <folly/json.h>
#include <folly/logging/LogConfigParser.h>
#include <folly/logging/LoggerDB.h>

using folly::LogConfig;
using folly::LoggerDB;
using folly::StringPiece;

namespace {
std::string configToStr(const LogConfig& config) {
  auto dyn = folly::logConfigToDynamic(config);
  folly::json::serialization_opts opts;
  opts.pretty_formatting = true;
  opts.sort_keys = true;
  return folly::json::serialize(dyn, opts);
}
} // namespace

namespace facebook {
namespace fb303 {

void registerFollyLoggingOptionHandlers(
    StringPiece name,
    StringPiece nameFull,
    ServiceData* serviceData,
    LoggerDB* loggerDB) {
  if (serviceData == nullptr) {
    serviceData = fbData.ptr();
  }
  if (loggerDB == nullptr) {
    loggerDB = &LoggerDB::get();
  }

  serviceData->registerDynamicOption(
      name,
      [loggerDB]() { return configToStr(loggerDB->getConfig()); },
      [loggerDB](const std::string& value) {
        auto config = folly::parseLogConfig(value);
        loggerDB->updateConfig(config);
      });
  serviceData->registerDynamicOption(
      nameFull,
      [loggerDB]() { return configToStr(loggerDB->getFullConfig()); },
      [loggerDB](const std::string& value) {
        auto config = folly::parseLogConfig(value);
        loggerDB->resetConfig(config);
      });
}

} // namespace fb303
} // namespace facebook
