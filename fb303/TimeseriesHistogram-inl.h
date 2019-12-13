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

#include <sstream>

namespace facebook {
namespace fb303 {

template <typename T>
std::string TimeseriesHistogram<T>::debugString() const {
  std::ostringstream o;

  o << "num buckets: " << BaseType::getNumBuckets()
    << ", bucketSize: " << BaseType::getBucketSize()
    << ", min: " << BaseType::getMin() << ", max: " << BaseType::getMax()
    << "\n";

  for (size_t i = 0; i < BaseType::getNumBuckets(); i++) {
    o << "  " << BaseType::getBucketMin(i) << "\n";
  }

  return o.str();
}

template <typename T>
std::ostream& operator<<(std::ostream& o, const TimeseriesHistogram<T>& h) {
  return o << h.debugString();
}

} // namespace fb303
} // namespace facebook
