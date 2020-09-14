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

#include <fb303/ServiceData.h>

#include <boost/regex.hpp>
#include <stdexcept>

#include <fb303/LegacyClock.h>
#include <folly/Conv.h>
#include <folly/ExceptionString.h>
#include <folly/Indestructible.h>
#include <folly/MapUtil.h>
#include <folly/String.h>
#include <gflags/gflags.h>

using folly::StringPiece;
using std::map;
using std::string;
using std::vector;

namespace facebook {
namespace fb303 {

template <typename T>
static T& as_mutable(T const& t) {
  return const_cast<T&>(t);
}

/*
 * The constructor used to create additional ServiceData instances.
 * IMPORTANT NOTE: There already is a global singleton instance living,
 * which is accessible via the static "get()" method or via the classic
 * "fbData->" style (both access the same single global instance).
 */
ServiceData::ServiceData()
    : aliveSince_(time(nullptr)),
      useOptionsAsFlags_(false),
      dynamicCounters_(),
      statsMap_(&dynamicCounters_),
      histMap_(
          &dynamicCounters_,
          &dynamicStrings_,
          ExportedHistogram(1000, 0, 10000)) {}

ServiceData::~ServiceData() {}

std::shared_ptr<ServiceData> ServiceData::getShared() {
  static folly::Indestructible<std::shared_ptr<ServiceData>> serviceData(
      std::make_unique<ServiceData>());
  return *serviceData;
}

ServiceData* ServiceData::get() {
  static auto serviceData = getShared().get();
  return serviceData;
}

void ServiceData::resetAllData() {
  options_.wlock()->clear();

  counters_.wlock()->clear();

  exportedValues_.wlock()->clear();

  statsMap_.forgetAllStats();
  histMap_.forgetAllHistograms();

  dynamicStrings_.clear();
  dynamicCounters_.clear();
}

void ServiceData::zeroStats() {
  counters_.withRLock([&](auto const& counters) {
    for (auto const& elem : counters) {
      //  this const-cast is safe: the lock protects the map structure only
      as_mutable(elem.second).store(0, std::memory_order_relaxed);
    }
  });

  statsMap_.clearAllStats();
  histMap_.clearAllHistograms();
}

void ServiceData::addStatExportType(
    StringPiece key,
    ExportType type,
    const ExportedStat* statPrototype) {
  statsMap_.exportStat(key, type, statPrototype);
}

void ServiceData::addStatExports(
    StringPiece key,
    StringPiece stats,
    int64_t bucketSize,
    int64_t min,
    int64_t max,
    const ExportedStat* statPrototype) {
  if (histMap_.contains(key)) {
    return; // already exists
  }
  bool addedHist = false;
  vector<string> statsSplit;
  folly::split(",", stats, statsSplit);
  for (const string& stat : statsSplit) {
    if (stat == "AVG") {
      statsMap_.exportStat(key, AVG, statPrototype);
    } else if (stat == "RATE") {
      statsMap_.exportStat(key, RATE, statPrototype);
    } else if (stat == "SUM") {
      statsMap_.exportStat(key, SUM, statPrototype);
    } else if (stat == "COUNT") {
      statsMap_.exportStat(key, COUNT, statPrototype);
    } else { // No match on stat type - assume it's a histogram percentile
      if (!addedHist) {
        if (bucketSize <= 0) {
          throw std::runtime_error(folly::to<string>(
              "bucketSize for ",
              key,
              " must be greater than zero (",
              bucketSize,
              ")"));
        }
        ExportedHistogram hist(
            bucketSize,
            min,
            max,
            statPrototype != nullptr ? *statPrototype
                                     : histMap_.getDefaultStat());
        histMap_.addHistogram(key, &hist);
        addedHist = true;
      }
      exportHistogramPercentile(key, folly::to<int32_t>(stat));
    }
  }
}

void ServiceData::addStatValue(StringPiece key, int64_t value) {
  statsMap_.addValue(key, get_legacy_stats_time(), value);
}

void ServiceData::addStatValue(
    StringPiece key,
    int64_t value,
    ExportType exportType) {
  statsMap_.addValue(key, get_legacy_stats_time(), value, exportType);
}

void ServiceData::addStatValueAggregated(
    StringPiece key,
    int64_t sum,
    int64_t numSamples) {
  statsMap_.addValueAggregated(key, get_legacy_stats_time(), sum, numSamples);
}

bool ServiceData::addHistogram(
    StringPiece key,
    int64_t bucketSize,
    int64_t min,
    int64_t max) {
  return histMap_.addHistogram(key, bucketSize, min, max);
}

bool ServiceData::addHistogram(StringPiece key, const ExportedHistogram& hist) {
  return histMap_.addHistogram(key, &hist);
}

void ServiceData::exportHistogramPercentile(StringPiece key, int pct) {
  histMap_.exportPercentile(key, pct);
}

void ServiceData::exportHistogram(StringPiece key, ExportType stat) {
  histMap_.exportStat(key, stat);
}

void ServiceData::exportHistogram(StringPiece key, int pct) {
  histMap_.exportStat(key, pct);
}

std::shared_ptr<QuantileStat> ServiceData::getQuantileStat(
    folly::StringPiece name,
    folly::Range<const ExportType*> stats,
    folly::Range<const double*> quantiles,
    folly::Range<const size_t*> slidingWindowPeriods) {
  auto stat = quantileMap_.get(name);
  if (stat) {
    return stat;
  }

  std::vector<QuantileStat::SlidingWindow> slidingWindowDefs;
  slidingWindowDefs.reserve(slidingWindowPeriods.size());
  for (const auto& slidingWindowLength : slidingWindowPeriods) {
    if (slidingWindowLength >= 60) {
      auto duration = std::chrono::seconds{slidingWindowLength};
      CHECK_EQ(0, duration.count() % 60);
      slidingWindowDefs.emplace_back(duration / 60, 60);
    } else {
      slidingWindowDefs.emplace_back(
          std::chrono::seconds(1), slidingWindowLength);
    }
  }
  stat = std::make_shared<QuantileStat>(std::move(slidingWindowDefs));

  std::vector<detail::QuantileStatMap::StatDef> statDefs;
  statDefs.reserve(stats.size() + quantiles.size());
  for (auto statType : stats) {
    detail::QuantileStatMap::StatDef statDef;
    statDef.type = statType;
    statDefs.push_back(statDef);
  }
  for (auto quantile : quantiles) {
    detail::QuantileStatMap::StatDef statDef;
    statDef.type = ExportType::PERCENT;
    statDef.quantile = quantile;
    statDefs.push_back(statDef);
  }

  return quantileMap_.registerQuantileStat(
      name, std::move(stat), std::move(statDefs));
}

void ServiceData::addHistogramValue(
    StringPiece key,
    int64_t value,
    bool checkContains) {
  if (!checkContains || histMap_.contains(key)) {
    histMap_.addValue(key, get_legacy_stats_time(), value);
  }
}

void ServiceData::addHistogramValueMult(
    StringPiece key,
    int64_t value,
    int64_t times,
    bool checkContains) {
  if (!checkContains || histMap_.contains(key)) {
    histMap_.addValue(key, get_legacy_stats_time(), value, times);
  }
}

void ServiceData::addHistAndStatValue(
    StringPiece key,
    int64_t value,
    bool checkContains) {
  time_t now = get_legacy_stats_time();
  statsMap_.addValue(key, now, value);

  if (!checkContains || histMap_.contains(key)) {
    histMap_.addValue(key, now, value);
  }
}

void ServiceData::addHistAndStatValues(
    StringPiece key,
    const folly::Histogram<int64_t>& values,
    time_t now,
    int64_t sum,
    int64_t nsamples,
    bool checkContains) {
  statsMap_.addValueAggregated(key, now, sum, nsamples);

  if (!checkContains || histMap_.contains(key)) {
    histMap_.addValues(key, now, values);
  }
}

int64_t ServiceData::incrementCounter(StringPiece key, int64_t amount) {
  {
    //  optimistically, the key is certainly present; update under rlock
    auto countersRLock = counters_.rlock();
    if (auto ptr = folly::get_ptr(*countersRLock, key)) {
      //  this const-cast is safe: the lock protects the map structure only
      auto& ref = as_mutable(*ptr);
      return ref.fetch_add(amount, std::memory_order_relaxed) + amount;
    }
  }

  //  pessimistically, the key is possibly absent; upsert under wlock
  auto countersWLock = counters_.wlock();
  auto& ref = (*countersWLock)[key];
  return ref.fetch_add(amount, std::memory_order_relaxed) + amount;
}

int64_t ServiceData::setCounter(StringPiece key, int64_t value) {
  {
    //  optimistically, the key is certainly present; update under rlock
    auto countersRLock = counters_.rlock();
    if (auto ptr = folly::get_ptr(*countersRLock, key)) {
      //  this const-cast is safe: the lock protects the map structure only
      auto& ref = as_mutable(*ptr);
      ref.store(value, std::memory_order_relaxed);
      return value;
    }
  }

  //  pessimistically, the key is possibly absent; upsert under wlock
  auto countersWLock = counters_.wlock();
  auto& ref = (*countersWLock)[key];
  ref.store(value, std::memory_order_relaxed);
  return value;
}

void ServiceData::clearCounter(StringPiece key) {
  auto countersWLock = counters_.wlock();
  auto it = countersWLock->find(key);
  if (it != countersWLock->end()) {
    countersWLock->erase(it);
  }
}

folly::Optional<int64_t> ServiceData::getCounterIfExists(
    StringPiece key) const {
  int64_t ret;
  if (dynamicCounters_.getCounter(key, &ret)) {
    return ret;
  }

  auto quantileValue = quantileMap_.getValue(key);
  if (quantileValue) {
    return quantileValue;
  }

  auto countersRLock = counters_.rlock();
  auto ptr = folly::get_ptr(*countersRLock, key);
  return ptr ? folly::make_optional(ptr->load(std::memory_order_relaxed))
             : folly::none;
}

int64_t ServiceData::getCounter(StringPiece key) const {
  folly::Optional<int64_t> ret = getCounterIfExists(key);

  if (ret.has_value()) {
    return *ret;
  }
  throw std::invalid_argument(
      folly::to<string>("no such counter \"", key, "\""));
}

void ServiceData::getCounters(map<string, int64_t>& _return) const {
  counters_.withRLock([&](auto const& counters) {
    for (auto const& elem : counters) {
      _return[elem.first] = elem.second.load(std::memory_order_relaxed);
    }
  });

  quantileMap_.getValues(_return);

  dynamicCounters_.getCounters(&_return);
}

std::vector<std::string> ServiceData::getCounterKeys() const {
  std::vector<std::string> keys;
  counters_.withRLock([&](auto const& counters) {
    keys.reserve(counters.size());
    for (const auto& elem : counters) {
      keys.push_back(elem.first);
    }
  });

  quantileMap_.getKeys(keys);

  dynamicCounters_.getKeys(&keys);

  return keys;
}

uint64_t ServiceData::getNumCounters() const {
  int64_t numCounters = 0;

  counters_.withRLock(
      [&](auto const& counters) { numCounters += counters.size(); });

  numCounters += quantileMap_.getNumKeys();

  numCounters += dynamicCounters_.getNumKeys();

  return numCounters;
}

map<string, int64_t> ServiceData::getCounters() const {
  map<string, int64_t> _return;
  getCounters(_return);
  return _return;
}

void ServiceData::getSelectedCounters(
    map<string, int64_t>& _return,
    const vector<string>& keys) const {
  quantileMap_.getSelectedValues(_return, keys);
  for (const string& key : keys) {
    if (_return.find(key) == _return.end()) {
      try {
        int64_t value = getCounter(key);
        _return[key] = value;
      } catch (const std::invalid_argument&) {
        // Don't insert anything into _return for non-existent keys.
      }
    }
  }
}

map<string, int64_t> ServiceData::getSelectedCounters(
    const vector<string>& keys) const {
  map<string, int64_t> _return;
  getSelectedCounters(_return, keys);
  return _return;
}

void ServiceData::getRegexCounters(
    map<string, int64_t>& _return,
    const string& regex) const {
  const boost::regex regexObject(regex);

  auto keys = getCounterKeys();
  keys.erase(
      std::remove_if(
          keys.begin(),
          keys.end(),
          [&](const std::string& key) {
            return !regex_match(key, regexObject);
          }),
      keys.end());

  getSelectedCounters(_return, keys);
}

map<string, int64_t> ServiceData::getRegexCounters(const string& regex) const {
  map<string, int64_t> _return;
  getRegexCounters(_return, regex);
  return _return;
}

bool ServiceData::hasCounter(StringPiece key) const {
  if (dynamicCounters_.contains(key)) {
    return true;
  }

  if (quantileMap_.contains(key)) {
    return true;
  }

  return counters_.rlock()->count(key) != 0;
}

void ServiceData::deleteExportedKey(StringPiece key) {
  if (exportedValues_.rlock()->count(key) == 0) {
    return;
  }

  auto exportedValuesULock = exportedValues_.ulock();
  auto const it = exportedValuesULock->find(key);
  if (it == exportedValuesULock->end()) {
    return;
  }

  auto exportedValuesWLock = exportedValuesULock.moveFromUpgradeToWrite();
  exportedValuesWLock->erase(it);
}

void ServiceData::setExportedValue(StringPiece key, string value) {
  {
    auto exportedValuesRLock = exportedValues_.rlock();
    if (auto ptr = folly::get_ptr(*exportedValuesRLock, key)) {
      as_mutable(*ptr).swap(value);
      return;
    }
  }

  auto exportedValuesWLock = exportedValues_.wlock();
  auto& entry = (*exportedValuesWLock)[key];

  auto exportedValuesRLock = exportedValuesWLock.moveFromWriteToRead();
  entry.swap(value);
}

void ServiceData::getExportedValue(string& _return, StringPiece key) const {
  if (dynamicStrings_.getValue(key, &_return)) {
    return;
  }

  auto exportedValuesRLock = exportedValues_.rlock();
  if (auto ptr = folly::get_ptr(*exportedValuesRLock, key)) {
    _return = ptr->copy();
  }
}

string ServiceData::getExportedValue(StringPiece key) const {
  string _return;
  getExportedValue(_return, key);
  return _return;
}

void ServiceData::getExportedValues(map<string, string>& _return) const {
  exportedValues_.withRLock([&](auto const& exportedValues) {
    for (auto const& elem : exportedValues) {
      _return[elem.first] = elem.second.copy();
    }
  });

  dynamicStrings_.getValues(&_return);
}

map<string, string> ServiceData::getExportedValues() const {
  map<string, string> _return;
  getExportedValues(_return);
  return _return;
}

void ServiceData::getSelectedExportedValues(
    map<string, string>& _return,
    const vector<string>& keys) const {
  exportedValues_.withRLock([&](auto const& exportedValues) {
    for (auto const& key : keys) {
      if (auto ptr = folly::get_ptr(exportedValues, key)) {
        _return[key] = ptr->copy();
      }
    }
  });

  for (auto const& key : keys) {
    string dynamicValue;
    if (dynamicStrings_.getValue(key, &dynamicValue)) {
      _return[key] = dynamicValue;
    }
  }
}

map<string, string> ServiceData::getSelectedExportedValues(
    const vector<string>& keys) const {
  map<string, string> _return;
  getSelectedExportedValues(_return, keys);
  return _return;
}

void ServiceData::getRegexExportedValues(
    map<string, string>& _return,
    const string& regex) const {
  const boost::regex regexObject(regex);
  map<string, string> allExportedValues;

  getExportedValues(allExportedValues);

  for (auto elem : allExportedValues) {
    if (regex_match(elem.first, regexObject)) {
      _return[elem.first] = elem.second;
    }
  }
}

map<string, string> ServiceData::getRegexExportedValues(
    const string& regex) const {
  map<string, string> _return;
  getRegexExportedValues(_return, regex);
  return _return;
}

void ServiceData::setUseOptionsAsFlags(bool useOptionsAsFlags) {
  if (useOptionsAsFlags) {
    LOG(WARNING) << "setUseOptionsAsFlags is a dangerous API and can expose "
                 << "your service to a Remote Code Execution vulnerability. "
                 << "Please consider using alternative methods like "
                 << "configerator to set properties dynamically";
  }
  useOptionsAsFlags_.store(useOptionsAsFlags, std::memory_order_relaxed);
}

bool ServiceData::getUseOptionsAsFlags() const {
  return useOptionsAsFlags_.load(std::memory_order_relaxed);
}

void ServiceData::setOption(StringPiece key, StringPiece value) {
  try {
    setOptionThrowIfAbsent(key, value);
  } catch (const std::exception& ex) {
    LOG(ERROR) << folly::exceptionStr(ex);
  }
}

void ServiceData::setOptionThrowIfAbsent(StringPiece key, StringPiece value) {
  {
    auto dynamicOptionsRLock = dynamicOptions_.rlock();
    if (auto ptr = folly::get_ptr(*dynamicOptionsRLock, key)) {
      if (ptr->setter) {
        as_mutable(ptr->setter)(value.str());
      }
      return;
    }
  }

  (*options_.wlock())[key] = value.str();

  // By default allow modifying glog verbosity (options 'v' or 'vmodule')
  auto useOptionsAsFlags = useOptionsAsFlags_.load(std::memory_order_relaxed);
  static constexpr StringPiece blacklistedOptions[] = {
      StringPiece{"logmailer"}, StringPiece{"whitelist_flags"}};
  if (std::count(
          std::begin(blacklistedOptions), std::end(blacklistedOptions), key) >
      0) {
    throw std::runtime_error(
        folly::to<string>("Cannot change blacklisted flag: ", key));
  }

  if (!(useOptionsAsFlags || key == "v" || key == "vmodule")) {
    throw std::runtime_error("Runtime flag changes were not enabled");
  }

  setOptionAsFlagsThrowIfAbsent(key.str(), value.str());
}

void ServiceData::setOptionAsFlags(const string& key, const string& value) {
  try {
    setOptionAsFlagsThrowIfAbsent(key, value);
  } catch (const std::exception& ex) {
    LOG(ERROR) << folly::exceptionStr(ex);
  }
}

void ServiceData::setOptionAsFlagsThrowIfAbsent(
    const string& key,
    const string& value) {
  string res = gflags::SetCommandLineOption(key.c_str(), value.c_str());
  if (res.empty()) {
    LOG(ERROR) << "Couldn't set flag 'FLAGS_" << key << "' to val '" << value
               << "'";
    throw std::runtime_error(folly::to<string>(
        "Failed SetCommandLineOption for flag: ", key, " value: ", value));
  }
  // special handling for vmodule changes as SetCommandLineOption()
  // is not sufficient. Need to call SetVLOGLevel() as well.
  if (key == "vmodule") {
    setVModuleOption(key, value);
  } else if (key == "v") {
    gflags::SetCommandLineOption("minloglevel", "0");
  }
  LOG(WARNING) << "FLAG CHANGE: overrode 'FLAGS_" << key << "' to val '"
               << value << "', res '" << res << "'";
}

void ServiceData::setVModuleOption(StringPiece /*key*/, StringPiece value) {
  vector<string> values;
  folly::split(",", value, values);
  for (size_t i = 0; i < values.size(); ++i) {
    vector<string> module_value;
    folly::split("=", values[i], module_value);
    if (module_value.size() != 2) {
      LOG(WARNING) << "Invalid vmodule value: " << values[i]
                   << ". Expected <module>=<int>";
      continue;
    }
    int level = atoi(module_value[1].c_str());
    LOG(INFO) << "Setting vmodule: " << module_value[0] << " to " << level;
    google::SetVLOGLevel(module_value[0].c_str(), level);
  }
  // if any of vmodule or v are specified, enable vlog'ing.
  gflags::SetCommandLineOption("minloglevel", "0");
}

string ServiceData::getOption(StringPiece key) const {
  {
    auto dynamicOptionsRLock = dynamicOptions_.rlock();
    if (auto ptr = folly::get_ptr(*dynamicOptionsRLock, key)) {
      return ptr->getter ? as_mutable(ptr->getter)() : string();
    }
  }

  {
    auto optionsRLock = options_.rlock();
    if (auto ptr = folly::get_ptr(*optionsRLock, key)) {
      return *ptr;
    }
  }

  string ret;
  if (gflags::GetCommandLineOption(key.str().c_str(), &ret)) {
    return ret;
  }

  throw std::invalid_argument(
      folly::to<string>("no such option \"", key, "\""));
}

void ServiceData::getOptions(map<string, string>& _return) const {
  _return.clear();

  options_.withRLock([&](auto const& options) {
    for (auto const& entry : options) {
      _return[entry.first] = entry.second;
    }
  });

  dynamicOptions_.withRLock([&](auto const& dynamicOptions) {
    for (const auto& entry : dynamicOptions) {
      string value;
      if (entry.second.getter) {
        try {
          value = as_mutable(entry.second).getter();
        } catch (const std::exception& ex) {
          value = folly::to<string>("<error: ", ex.what(), ">");
        }
      }
      _return[entry.first] = value;
    }
  });

  if (useOptionsAsFlags_.load(std::memory_order_relaxed)) {
    this->mergeOptionsWithGflags(_return);
  }
}

map<string, string> ServiceData::getOptions() const {
  map<string, string> _return;
  getOptions(_return);
  return _return;
}

void ServiceData::mergeOptionsWithGflags(map<string, string>& _return) const {
  vector<gflags::CommandLineFlagInfo> allFlags;

  gflags::GetAllFlags(&allFlags);

  for (const auto& entry : allFlags) {
    _return[entry.name] = entry.current_value;
  }
  return;
}

void ServiceData::registerDynamicOption(
    StringPiece name,
    DynamicOptionGetter getter,
    DynamicOptionSetter setter) {
  auto option = DynamicOption(std::move(getter), std::move(setter));
  std::swap((*dynamicOptions_.wlock())[name], option);
}

} // namespace fb303
} // namespace facebook
