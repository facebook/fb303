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

#include <fb303/BaseService.h>

#include <thrift/lib/cpp2/Flags.h>

THRIFT_FLAG_DEFINE_int64(fb303_counters_queue_timeout_ms, 5 * 1000);

namespace facebook {
namespace fb303 {

/* static */ thread_local bool BaseService::useRegexCacheTL_ = false;

BaseService::~BaseService() {}

std::chrono::milliseconds BaseService::getCountersExpiration() const {
  return getCountersExpiration_
      ? *getCountersExpiration_
      : std::chrono::milliseconds(THRIFT_FLAG(fb303_counters_queue_timeout_ms));
}

} // namespace fb303
} // namespace facebook
