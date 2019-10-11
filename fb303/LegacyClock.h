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

#include <time.h>

namespace facebook {
namespace fb303 {

/**
 * The stats code traditionally measured time with the wall clock at
 * second (time_t) precision. This function is equivalent to calling
 * ::time(nullptr) except that it's faster than ::time on macOS.
 *
 * The values returned by this function also correspond with,
 * std::chrono::system_clock and CLOCK_REALTIME.
 *
 * Eventually the fb303 stats code should migrate to using
 * std::chrono::steady_clock.
 */
time_t get_legacy_stats_time();

} // namespace fb303
} // namespace facebook
