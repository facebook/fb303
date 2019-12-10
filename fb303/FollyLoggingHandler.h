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

#include <folly/Range.h>

namespace folly {
class LoggerDB;
} // namespace folly

namespace facebook {
namespace fb303 {

class ServiceData;

/**
 * This registers dynamic option handlers to support configuring the folly
 * logging library via fb303 setOption() and getOption() calls.
 *
 * This creates two options, named "logging" and "logging_full" by default.
 *
 * - Getting the value of the "logging" option returns the current logging
 *   configuration settings.  This lists the configuration for all categories
 *   that have non-default settings.
 *
 * - Getting the value of the "logging_full" option returns all current logging
 *   configuration settings.  This lists the configuration for *all*
 *   categories, including ones that are using default settings.
 *
 * - Setting the value of the "logging" option updates the current logging
 *   configuration with the additional settings specified.
 *
 * - Setting the value of the "logging_full" option replaces all current
 *   logging settings with the new settings.  Any categories not listed in the
 *   new configuration are reset to their default configuration state.
 *
 * If the serviceData parameter is null it defaults to the default ServiceData
 * singleton (facebook::fbData).
 * If the loggerDB parameter is null it defaults to the default LoggerDB
 * singleton.
 */
void registerFollyLoggingOptionHandlers(
    folly::StringPiece name = "logging",
    folly::StringPiece nameFull = "logging_full",
    ServiceData* serviceData = nullptr,
    folly::LoggerDB* loggerDB = nullptr);

} // namespace fb303
} // namespace facebook
