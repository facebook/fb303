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

#include <fb303/detail/RegexUtil.h>
#include <fmt/core.h>
#include <folly/MapUtil.h>

namespace facebook {
namespace fb303 {
namespace detail {

template <typename ClockT>
std::chrono::seconds statDuration(
    const folly::Optional<std::chrono::seconds>& slidingWindowLength,
    typename ClockT::time_point creationTime) {
  auto now = ClockT::now();
  auto diff =
      std::chrono::duration_cast<std::chrono::seconds>(now - creationTime);
  return (!slidingWindowLength || *slidingWindowLength > diff)
      ? diff
      : *slidingWindowLength;
}

template <typename ClockT>
folly::Optional<int64_t> BasicQuantileStatMap<ClockT>::getValue(
    folly::StringPiece key) const {
  CounterMapEntry cme;
  {
    auto countersRLock = counters_.rlock();
    auto it = countersRLock->map.find(key);
    if (it == countersRLock->map.end()) {
      return folly::none;
    }
    cme = it->second;
  }
  folly::Range<const double*> r;
  if (cme.statDef.type == ExportType::PERCENT) {
    r = folly::Range<const double*>(&cme.statDef.quantile, 1);
  }

  auto estimates = cme.stat->getEstimates(r);

  const folly::QuantileEstimates* qe = nullptr;
  if (cme.slidingWindowLength) {
    for (const auto& slidingWindow : estimates.slidingWindows) {
      auto slidingWindowLength = slidingWindow.slidingWindowLength();
      if (slidingWindowLength == cme.slidingWindowLength) {
        qe = &slidingWindow.estimate;
        break;
      }
    }
  } else {
    qe = &estimates.allTimeEstimate;
  }

  if (!qe) {
    return folly::none;
  }

  return extractValue(
      cme.statDef,
      *qe,
      statDuration<ClockT>(cme.slidingWindowLength, cme.stat->creationTime()));
}

template <typename ClockT>
void BasicQuantileStatMap<ClockT>::getValues(
    std::map<std::string, int64_t>& out) const {
  auto now = ClockT::now();
  // Note: Assume that stats get added rarely, so hold the read lock for the
  // entire time rather than copy the map.
  auto countersRLock = counters_.rlock();
  for (const auto& [key, sme] : countersRLock->bases) {
    std::vector<double> quantiles;
    for (const auto& statDef : sme.statDefs) {
      if (statDef.type == ExportType::PERCENT) {
        quantiles.push_back(statDef.quantile);
      }
    }
    auto estimates = sme.stat->getEstimates(quantiles, now);
    auto timeSinceCreation = std::chrono::duration_cast<std::chrono::seconds>(
        now - sme.stat->creationTime());
    for (const auto& statDef : sme.statDefs) {
      addValues(key, statDef, estimates, timeSinceCreation, out);
    }
  }
}

template <typename ClockT>
void BasicQuantileStatMap<ClockT>::getSelectedValues(
    std::map<std::string, int64_t>& out,
    const std::vector<std::string>& keys) const {
  std::map<
      stat_type*,
      std::vector<std::pair<const std::string*, CounterMapEntry>>>
      stats;
  {
    auto countersRLock = counters_.rlock();
    for (const auto& key : keys) {
      auto it = countersRLock->map.find(key);
      if (it != countersRLock->map.end()) {
        stats[it->second.stat.get()].emplace_back(&key, it->second);
      }
    }
  }
  auto now = ClockT::now();
  for (const auto& [stat, vec] : stats) {
    std::vector<double> quantiles;
    for (const auto& [_, cme] : vec) {
      if (cme.statDef.type == ExportType::PERCENT) {
        quantiles.push_back(cme.statDef.quantile);
      }
    }
    auto estimates = stat->getEstimates(quantiles, now);
    auto timeSinceCreation = std::chrono::duration_cast<std::chrono::seconds>(
        now - stat->creationTime());
    for (const auto& [pkey, cme] : vec) {
      if (cme.slidingWindowLength) {
        for (const auto& slidingWindow : estimates.slidingWindows) {
          auto slidingWindowLength = slidingWindow.slidingWindowLength();
          if (slidingWindowLength == *cme.slidingWindowLength) {
            auto duration = std::min(slidingWindowLength, timeSinceCreation);
            out[*pkey] =
                extractValue(cme.statDef, slidingWindow.estimate, duration);
            break;
          }
        }
      } else {
        out[*pkey] = extractValue(
            cme.statDef, estimates.allTimeEstimate, timeSinceCreation);
      }
    }
  }
}

template <typename ClockT>
std::shared_ptr<BasicQuantileStat<ClockT>> BasicQuantileStatMap<ClockT>::get(
    folly::StringPiece name) const {
  auto countersRLock = counters_.rlock();
  auto it = countersRLock->bases.find(name);
  if (it != countersRLock->bases.end()) {
    return it->second.stat;
  }
  return nullptr;
}

template <typename ClockT>
bool BasicQuantileStatMap<ClockT>::contains(folly::StringPiece name) const {
  auto countersRLock = counters_.rlock();
  auto it = countersRLock->map.find(name);
  return (it != countersRLock->map.end());
}

template <typename ClockT>
void BasicQuantileStatMap<ClockT>::getKeys(
    std::vector<std::string>& keys) const {
  auto countersRLock = counters_.rlock();
  keys.reserve(keys.size() + countersRLock->map.size());
  for (const auto& [key, _] : countersRLock->map) {
    keys.emplace_back(key);
  }
}

template <typename ClockT>
void BasicQuantileStatMap<ClockT>::getRegexKeys(
    std::vector<std::string>& keys,
    const std::string& regex) const {
  getRegexKeysImpl(keys, regex, counters_);
}

template <typename ClockT>
size_t BasicQuantileStatMap<ClockT>::getNumKeys() const {
  auto countersRLock = counters_.rlock();
  return countersRLock->map.size();
}

template <typename ClockT>
folly::Optional<typename BasicQuantileStatMap<ClockT>::SnapshotEntry>
BasicQuantileStatMap<ClockT>::getSnapshotEntry(
    folly::StringPiece name,
    TimePoint now) const {
  auto countersRLock = counters_.rlock();
  auto it = countersRLock->bases.find(name);
  if (it == countersRLock->bases.end()) {
    return {};
  }
  SnapshotEntry entry;
  entry.name = name;
  entry.snapshot = it->second.stat->getSnapshot(now);
  entry.statDefs = it->second.statDefs;
  return entry;
}

template <typename ClockT>
std::shared_ptr<BasicQuantileStat<ClockT>>
BasicQuantileStatMap<ClockT>::registerQuantileStat(
    folly::StringPiece name,
    std::shared_ptr<BasicQuantileStat<ClockT>> stat,
    std::vector<BasicQuantileStatMap<ClockT>::StatDef> statDefs) {
  auto countersWLock = counters_.wlock();
  auto it = countersWLock->bases.find(name);
  if (it != countersWLock->bases.end()) {
    return it->second.stat;
  }
  for (const auto& statDef : statDefs) {
    CounterMapEntry entry;
    entry.stat = stat;
    entry.statDef = statDef;
    countersWLock->map.emplace(makeKey(name, statDef, folly::none), entry);

    auto slidingWindowLengths = stat->getSlidingWindowLengths();

    for (auto slidingWindowLength : slidingWindowLengths) {
      entry.slidingWindowLength = slidingWindowLength;
      countersWLock->map.emplace(
          makeKey(name, statDef, slidingWindowLength), entry);
    }

    // avoid fetch_add() to avoid extra fences, since we hold the lock already
    uint64_t epoch = countersWLock->mapEpoch.load();
    countersWLock->mapEpoch.store(epoch + 1);
  }
  StatMapEntry statMapEntry;
  statMapEntry.stat = stat;
  statMapEntry.statDefs = std::move(statDefs);
  countersWLock->bases.emplace(std::move(name), std::move(statMapEntry));
  return stat;
}

template <typename ClockT>
std::string BasicQuantileStatMap<ClockT>::makeKey(
    folly::StringPiece base,
    const BasicQuantileStatMap<ClockT>::StatDef& statDef,
    const folly::Optional<std::chrono::seconds>& slidingWindowLength) {
  std::string tail = slidingWindowLength
      ? fmt::format(".{}", slidingWindowLength->count())
      : "";
  switch (statDef.type) {
    case ExportType::PERCENT:
      return fmt::format("{}.p{:g}{}", base, statDef.quantile * 100.0, tail);
    case ExportType::SUM:
      return fmt::format("{}.sum{}", base, tail);
    case ExportType::COUNT:
      return fmt::format("{}.count{}", base, tail);
    case ExportType::AVG:
      return fmt::format("{}.avg{}", base, tail);
    case ExportType::RATE:
      return fmt::format("{}.rate{}", base, tail);
  }
  LOG(FATAL) << "Unknown export type: " << statDef.type;
  return "";
}

template <typename StatDef>
double extractValueImpl(
    const StatDef& statDef,
    const folly::QuantileEstimates& estimate,
    std::chrono::seconds duration) {
  switch (statDef.type) {
    case ExportType::PERCENT:
      for (const auto& pr : estimate.quantiles) {
        if (pr.first == statDef.quantile) {
          return pr.second;
        }
      }
      LOG(FATAL) << "Requested missing quantile: " << statDef.quantile;
    case ExportType::SUM:
      return estimate.sum;
    case ExportType::COUNT:
      return estimate.count;
    case ExportType::AVG:
      if (estimate.count > 0) {
        return estimate.sum / estimate.count;
      }
      return 0;
    case ExportType::RATE:
      if (duration.count() > 0) {
        const auto& numerator = FLAGS_fb303_qstat_legacy_use_count_for_rate
            ? estimate.count
            : estimate.sum;
        return numerator / duration.count();
      }
      return estimate.count;
  }
  LOG(FATAL) << "Unknown export type: " << statDef.type;
  return 0;
}

template <typename ClockT>
int64_t BasicQuantileStatMap<ClockT>::extractValue(
    const StatDef& statDef,
    const folly::QuantileEstimates& estimate,
    std::chrono::seconds duration) {
  return folly::constexpr_clamp_cast<int64_t>(
      extractValueImpl(statDef, estimate, duration));
}

template <typename ClockT>
void BasicQuantileStatMap<ClockT>::addValues(
    folly::StringPiece statName,
    const typename BasicQuantileStatMap<ClockT>::StatDef& statDef,
    const typename BasicQuantileStat<ClockT>::Estimates& estimates,
    std::chrono::seconds timeSinceCreation,
    std::map<std::string, int64_t>& out) {
  out.emplace(
      makeKey(statName, statDef, folly::none),
      extractValue(statDef, estimates.allTimeEstimate, timeSinceCreation));
  for (const auto& slidingWindow : estimates.slidingWindows) {
    auto slidingWindowLength = slidingWindow.slidingWindowLength();
    auto duration = std::min(slidingWindowLength, timeSinceCreation);
    out.emplace(
        makeKey(statName, statDef, slidingWindowLength),
        extractValue(statDef, slidingWindow.estimate, duration));
  }
}

} // namespace detail
} // namespace fb303
} // namespace facebook
