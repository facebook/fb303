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
    int num_levels,
    int num_buckets,
    const int* level_durations)
    : BaseType(
          num_buckets,
          num_levels,
          &(detail::convertToDuration<TimeType>(
              num_levels,
              level_durations))[0]) {
  // Note: the version of MultiLevelTimeSeries in folly is updated to work with
  // std::chrono::duration rather then integer/time_t directly. Therefore it
  // expects an array of std::chrono::duration in the constructor. However, we
  // have an array of integers here that we need to convert to an array of
  // std::chrono::duration. Rather than manually allocating an array in the heap
  // and manage the memory ourselves, we call convertToDuration() which return a
  // vector by value. We then use the returned vector as a temporary and pass
  // its internal as an array to folly::MultiLevelTimeSeries's constructor. The
  // temporary vector is guaranteed to be in scope. All memory allocation is
  // automatically cleaned up. The compiler also may be able to perform return
  // value optimization. Finally it's safe to pass the internal address of the
  // vector as an array. This is guaranteed by the standard. See
  // http://www.open-std.org/jtc1/sc22/wg21/docs/lwg-defects.html#69.
}

} // namespace fb303
} // namespace facebook
