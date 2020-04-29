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

#include <folly/Format.h>

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
    folly::SharedMutex::ReadHolder g(mutex_);
    auto it = counterMap_.find(key);
    if (it == counterMap_.end()) {
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
  folly::SharedMutex::ReadHolder g(mutex_);
  for (const auto& kv : statMap_) {
    std::vector<double> quantiles;
    for (const auto& statDef : kv.second.statDefs) {
      if (statDef.type == ExportType::PERCENT) {
        quantiles.push_back(statDef.quantile);
      }
    }
    auto estimates = kv.second.stat->getEstimates(quantiles, now);
    auto timeSinceCreation = std::chrono::duration_cast<std::chrono::seconds>(
        now - kv.second.stat->creationTime());
    for (const auto& statDef : kv.second.statDefs) {
      addValues(kv.first, statDef, estimates, timeSinceCreation, out);
    }
  }
}

template <typename ClockT>
void BasicQuantileStatMap<ClockT>::getSelectedValues(
    std::map<std::string, int64_t>& out,
    const std::vector<std::string>& keys) const {
  std::map<
      stat_type*,
      std::vector<std::pair<std::string, const CounterMapEntry*>>>
      stats;
  {
    folly::SharedMutex::ReadHolder g(mutex_);
    for (const auto& key : keys) {
      auto it = counterMap_.find(key);
      if (it != counterMap_.end()) {
        stats[it->second.stat.get()].push_back(
            std::make_pair(key, &it->second));
      }
    }
  }
  auto now = ClockT::now();
  for (const auto& stat_vec : stats) {
    std::vector<double> quantiles;
    for (const auto& key_cme : stat_vec.second) {
      if (key_cme.second->statDef.type == ExportType::PERCENT) {
        quantiles.push_back(key_cme.second->statDef.quantile);
      }
    }
    auto estimates = stat_vec.first->getEstimates(quantiles, now);
    auto timeSinceCreation = std::chrono::duration_cast<std::chrono::seconds>(
        now - stat_vec.first->creationTime());
    for (const auto& key_cme : stat_vec.second) {
      if (key_cme.second->slidingWindowLength) {
        for (const auto& slidingWindow : estimates.slidingWindows) {
          auto slidingWindowLength = slidingWindow.slidingWindowLength();
          if (slidingWindowLength == *key_cme.second->slidingWindowLength) {
            auto duration = std::min(slidingWindowLength, timeSinceCreation);
            out[key_cme.first] = extractValue(
                key_cme.second->statDef, slidingWindow.estimate, duration);
            break;
          }
        }
      } else {
        out[key_cme.first] = extractValue(
            key_cme.second->statDef,
            estimates.allTimeEstimate,
            timeSinceCreation);
      }
    }
  }
}

template <typename ClockT>
std::shared_ptr<BasicQuantileStat<ClockT>> BasicQuantileStatMap<ClockT>::get(
    folly::StringPiece name) const {
  folly::SharedMutex::ReadHolder g(mutex_);
  auto it = statMap_.find(name);
  if (it != statMap_.end()) {
    return it->second.stat;
  }
  return nullptr;
}

template <typename ClockT>
bool BasicQuantileStatMap<ClockT>::contains(folly::StringPiece name) const {
  folly::SharedMutex::ReadHolder g(mutex_);
  auto it = counterMap_.find(name);
  return it != counterMap_.end();
}

template <typename ClockT>
void BasicQuantileStatMap<ClockT>::getKeys(
    std::vector<std::string>& keys) const {
  folly::SharedMutex::ReadHolder g(mutex_);
  for (const auto& kv : counterMap_) {
    keys.push_back(kv.first);
  }
}

template <typename ClockT>
size_t BasicQuantileStatMap<ClockT>::getNumKeys() const {
  folly::SharedMutex::ReadHolder g(mutex_);
  return counterMap_.size();
}

template <typename ClockT>
std::shared_ptr<BasicQuantileStat<ClockT>>
BasicQuantileStatMap<ClockT>::registerQuantileStat(
    folly::StringPiece name,
    std::shared_ptr<BasicQuantileStat<ClockT>> stat,
    std::vector<BasicQuantileStatMap<ClockT>::StatDef> statDefs) {
  folly::SharedMutex::WriteHolder g(mutex_);
  auto it = statMap_.find(name);
  if (it != statMap_.end()) {
    return it->second.stat;
  }
  for (const auto& statDef : statDefs) {
    CounterMapEntry entry;
    entry.stat = stat;
    entry.statDef = statDef;
    counterMap_.insert(
        std::make_pair(makeKey(name, statDef, folly::none), entry));

    auto slidingWindowLengths = stat->getSlidingWindowLengths();

    for (auto slidingWindowLength : slidingWindowLengths) {
      entry.slidingWindowLength = slidingWindowLength;
      counterMap_.insert(
          std::make_pair(makeKey(name, statDef, slidingWindowLength), entry));
    }
  }
  StatMapEntry statMapEntry;
  statMapEntry.stat = stat;
  statMapEntry.statDefs = std::move(statDefs);
  statMap_.insert(std::make_pair(std::move(name), std::move(statMapEntry)));
  return stat;
}

template <typename ClockT>
std::string BasicQuantileStatMap<ClockT>::makeKey(
    folly::StringPiece base,
    const BasicQuantileStatMap<ClockT>::StatDef& statDef,
    const folly::Optional<std::chrono::seconds>& slidingWindowLength) {
  std::string tail = slidingWindowLength
      ? folly::format(".{}", slidingWindowLength->count()).str()
      : "";
  switch (statDef.type) {
    case ExportType::PERCENT:
      return folly::format("{}.p{}{}", base, statDef.quantile * 100.0, tail)
          .str();
    case ExportType::SUM:
      return folly::format("{}.sum{}", base, tail).str();
    case ExportType::COUNT:
      return folly::format("{}.count{}", base, tail).str();
    case ExportType::AVG:
      return folly::format("{}.avg{}", base, tail).str();
    case ExportType::RATE:
      return folly::format("{}.rate{}", base, tail).str();
    default:
      LOG(FATAL) << "Unknown export type: " << statDef.type;
      return "";
  }
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
    default:
      LOG(FATAL) << "Unknown export type: " << statDef.type;
      return 0;
  }
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
  out.insert(std::make_pair(
      makeKey(statName, statDef, folly::none),
      extractValue(statDef, estimates.allTimeEstimate, timeSinceCreation)));
  for (const auto& slidingWindow : estimates.slidingWindows) {
    auto slidingWindowLength = slidingWindow.slidingWindowLength();
    auto duration = std::min(slidingWindowLength, timeSinceCreation);
    out.insert(std::make_pair(
        makeKey(statName, statDef, slidingWindowLength),
        extractValue(statDef, slidingWindow.estimate, duration)));
  }
}

} // namespace detail
} // namespace fb303
} // namespace facebook
