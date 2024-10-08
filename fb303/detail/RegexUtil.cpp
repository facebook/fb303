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

void cachedFindMatchesCopyUnderSharedLock(
    std::vector<std::string>& out,
    folly::RegexMatchCache const& cache,
    std::string_view const regex,
    folly::RegexMatchCache::time_point const now) {
  auto const matches = cache.findMatchesUnsafe(regex, now);
  folly::grow_capacity_by(out, matches.size());
  for (auto const match : matches) {
    out.emplace_back(*match);
  }
}

} // namespace facebook::fb303::detail
