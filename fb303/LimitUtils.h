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

#include <string_view>

#include <thrift/lib/cpp2/server/Cpp2ConnContext.h>

namespace facebook::fb303 {

constexpr folly::StringPiece kCountersAvailableHeader{
    "fb303_counters_available"};
// Return an optional that contains a counter limit if one was specified
// in the request headers.
inline std::optional<size_t> readThriftHeader(
    apache::thrift::Cpp2RequestContext* reqCtx,
    std::string_view key) {
  if (reqCtx == nullptr) {
    return std::nullopt;
  }
  apache::thrift::transport::THeader* reqHeader = reqCtx->getHeader();
  if (reqHeader == nullptr) {
    return std::nullopt;
  }
  const apache::thrift::transport::THeader::StringToStringMap& headers =
      reqHeader->getHeaders();
  const std::string* val = folly::get_ptr(headers, key);
  if (val == nullptr) {
    return std::nullopt;
  }
  auto lim = folly::tryTo<int>(*val).value_or(-1);
  if (lim < 0) {
    return std::nullopt;
  }
  return lim;
}
// Write to the response header the number of counters available. Clients can
// use this header to know if the server is dropping data.
inline void addCountersAvailableToResponse(
    apache::thrift::Cpp2RequestContext* reqCtx,
    size_t available) {
  if (reqCtx == nullptr) {
    return;
  }
  auto* header = reqCtx->getHeader();
  if (header == nullptr) {
    return;
  }
  header->mutableWriteHeaders().emplace(
      kCountersAvailableHeader, std::to_string(available));
}
} // namespace facebook::fb303
