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

#include <fb303/CallbackValuesMap.h>

namespace facebook {
namespace fb303 {

using DynamicStrings = CallbackValuesMap<std::string>;

using CounterType = int64_t;

/**
 * A map of int64_t (CounterType) valued callbacks, with some extra
 * functions added for backwards compatibility with the previous version.
 */
class DynamicCounters : public CallbackValuesMap<CounterType> {
 public:
  typedef CallbackValuesMap<CounterType>::Callback Callback;

  // for backwards compat
  void getCounters(std::map<std::string, CounterType>* output) const {
    return getValues(output);
  }

  // for backwards compat
  bool getCounter(folly::StringPiece name, CounterType* output) const {
    return getValue(name, output);
  }
};

} // namespace fb303
} // namespace facebook
