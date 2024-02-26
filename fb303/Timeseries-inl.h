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

#pragma once

#include <vector>

namespace facebook {
namespace fb303 {

namespace detail {

template <class TimeType>
std::vector<TimeType> convertToDuration(
    int num_levels,
    const int* level_durations) {
  std::vector<TimeType> result;
  result.reserve(num_levels);
  for (int i = 0; i < num_levels; ++i) {
    result.emplace_back(std::chrono::duration_cast<TimeType>(
        std::chrono::seconds(level_durations[i])));
  }
  return result;
}

} // namespace detail

template <class T>
MultiLevelTimeSeries<T>::MultiLevelTimeSeries(
    size_t num_levels,
    size_t num_buckets,
    const int* level_durations)
    : BaseType(
          num_buckets,
          detail::convertToDuration<TimeType>(num_levels, level_durations)) {}

} // namespace fb303
} // namespace facebook
