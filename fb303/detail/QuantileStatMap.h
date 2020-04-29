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

#include <map>
#include <unordered_map>
#include <vector>

#include <gflags/gflags.h>

#include <folly/Optional.h>
#include <folly/SharedMutex.h>
#include <folly/experimental/StringKeyedUnorderedMap.h>

#include <fb303/ExportType.h>
#include <fb303/QuantileStat.h>

/**
 * Allow services to switch back to the old implementation for RATE. Eventually
 * we need to remove this flag and default to using sum for rate computation.
 */
DECLARE_bool(fb303_qstat_legacy_use_count_for_rate);

namespace facebook {
namespace fb303 {
namespace detail {

/*
 * QuantileStats don't use DynamicCounters because DynamicCounters would not
 * efficiently reuse the TDigest across quantile estimates.
 */
template <typename ClockT>
class BasicQuantileStatMap {
 public:
  using stat_type = BasicQuantileStat<ClockT>;

  struct StatDef {
    ExportType type;
    double quantile;
  };

  folly::Optional<int64_t> getValue(folly::StringPiece key) const;
  void getValues(std::map<std::string, int64_t>& out) const;
  void getSelectedValues(
      std::map<std::string, int64_t>& out,
      const std::vector<std::string>& keys) const;

  std::shared_ptr<stat_type> get(folly::StringPiece name) const;
  bool contains(folly::StringPiece name) const;
  void getKeys(std::vector<std::string>& keys) const;
  size_t getNumKeys() const;

  std::shared_ptr<stat_type> registerQuantileStat(
      folly::StringPiece name,
      std::shared_ptr<stat_type> stat,
      std::vector<StatDef> statDefs);

  // BasicQuantileStat buffers added values for a buffer duration.
  // This method can be used to force the buffers to be flushed and
  // rebuild the digests.
  void flushAll() {
    folly::SharedMutex::ReadHolder g(mutex_);
    for (auto& p : counterMap_) {
      if (p.second.stat != nullptr) {
        p.second.stat->flush();
      }
    }
  }

 private:
  struct CounterMapEntry {
    std::shared_ptr<stat_type> stat;
    StatDef statDef;
    folly::Optional<std::chrono::seconds> slidingWindowLength;
  };

  struct StatMapEntry {
    std::shared_ptr<stat_type> stat;
    std::vector<StatDef> statDefs;
  };

  folly::SharedMutex mutex_;

  // The key to this map is the fully qualified stat name, e.g. MyStat.p99.60
  folly::StringKeyedUnorderedMap<CounterMapEntry> counterMap_;

  // The key to this map is the base of the stat name, e.g. MyStat.
  folly::StringKeyedUnorderedMap<StatMapEntry> statMap_;

  static std::string makeKey(
      folly::StringPiece base,
      const StatDef& statDef,
      const folly::Optional<std::chrono::seconds>& slidingWindowLength);
  static int64_t extractValue(
      const StatDef& statDef,
      const folly::QuantileEstimates& estimate,
      std::chrono::seconds duration);
  static void addValues(
      folly::StringPiece statName,
      const StatDef& statDef,
      const typename stat_type::Estimates& estimate,
      std::chrono::seconds timeSinceCreation,
      std::map<std::string, int64_t>& out);
};

using QuantileStatMap = BasicQuantileStatMap<std::chrono::steady_clock>;

extern template class BasicQuantileStatMap<std::chrono::steady_clock>;

} // namespace detail
} // namespace fb303
} // namespace facebook

#include <fb303/detail/QuantileStatMap-inl.h>
