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

#include <fb303/detail/RegexUtil.h>

#include <boost/regex.hpp>

namespace facebook::fb303::detail {

using folly::StringKeyedMap;
using std::string;
using std::vector;

const size_t kRegexCacheLimit = 20000;
const size_t kRegexLengthLimit = 1024 * 1024;

void filterRegexKeys(vector<string>& keys, const string& regex) {
  const boost::regex regexObject(regex); // compile regex
  keys.erase(
      std::remove_if(
          keys.begin(),
          keys.end(),
          [&](const string& key) { return !regex_match(key, regexObject); }),
      keys.end());
}

void cacheRegexKeys(
    vector<string>& keys,
    const string& regex,
    StringKeyedMap<vector<string>>& cache) {
  // If this becomes an issue, we can use an SHA-256 of the regex
  if (regex.size() > kRegexLengthLimit) {
    return;
  }

  size_t regexCacheSize = 0;
  for (const auto& [key, vec] : cache) {
    regexCacheSize += vec.size();
  }
  if ((regexCacheSize + keys.size()) > kRegexCacheLimit) {
    // Size limit for cache - not expected to reach this limit unless
    // there is an anomaly, in which case it reverts to original behavior for
    // other keys.  Anything like "".*" regex pattern is expected to
    // match all keys and is likely to overflow this memory allotment and will
    // not get cached.
    return;
  }

  auto& vec = cache[regex];
  vec.swap(keys); // !!! transfer contents
}

} // namespace facebook::fb303::detail
