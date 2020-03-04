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

#include <fb303/DynamicCounters.h>
#include <fb303/ExportedStatMapImpl.h>
#include <fb303/MutexWrapper.h>
#include <fb303/TimeseriesHistogram.h>

namespace facebook {
namespace fb303 {

using ExportedHistogram = TimeseriesHistogram<CounterType>;
using HistogramPtr =
    std::shared_ptr<folly::Synchronized<ExportedHistogram, MutexWrapper>>;

class HistogramExporter {
 public:
  static void exportPercentile(
      const HistogramPtr& hist,
      folly::StringPiece name,
      int percentile,
      DynamicCounters* counters);

  static void unexportPercentile(
      const HistogramPtr& hist,
      folly::StringPiece name,
      int percentile,
      DynamicCounters* counters);

  static void exportStat(
      const HistogramPtr& hist,
      folly::StringPiece name,
      ExportType exportType,
      DynamicCounters* counters);

  static void unexportStat(
      const HistogramPtr& hist,
      folly::StringPiece name,
      ExportType exportType,
      DynamicCounters* counters);

  static void exportBuckets(
      const HistogramPtr& hist,
      folly::StringPiece name,
      DynamicStrings* strings);

 private:
  template <typename Fn>
  static void forEachPercentileName(
      const HistogramPtr& hist,
      folly::StringPiece name,
      int percentile,
      const Fn& fn);

  template <typename Fn>
  static void forEachStatName(
      const HistogramPtr& hist,
      folly::StringPiece name,
      ExportType exportType,
      const Fn& fn);
};

} // namespace fb303
} // namespace facebook
