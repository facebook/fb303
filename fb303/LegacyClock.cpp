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

#include <fb303/LegacyClock.h>

#include <stdexcept>

#include <folly/lang/Exception.h>

namespace facebook {
namespace fb303 {

time_t get_legacy_stats_time() {
#ifdef __APPLE__
  timespec ts;
  auto ret = clock_gettime(CLOCK_REALTIME, &ts);
  if (folly::kIsDebug && (ret != 0)) {
    folly::throw_exception<std::runtime_error>("Error using CLOCK_REALTIME.");
  }
  return ts.tv_sec;
#else
  return ::time(nullptr);
#endif
}

} // namespace fb303
} // namespace facebook
