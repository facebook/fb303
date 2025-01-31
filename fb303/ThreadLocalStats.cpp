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

#include <fb303/ThreadLocalStats.h>

#include <thread>

#include <gflags/gflags.h>

DEFINE_bool(
    fb303_tcData_dont_update_on_read,
    false,
    "If set, timeseries owned by thread-local timeseries classes will never be updated "
    "in between aggregation. It fixes a bug that is causing "
    "rapid false oscillations in all timeseries (most noticeable "
    "with aggregation intervals <= 10s");

namespace facebook::fb303 {

/*
 * Explicitly instantiate the commonly-used instantations of ThreadLocalStatsT.
 */

// Explicitly instantiate ThreadLocalStatsT and related classes
// when used with TLStatsNoLocking.
template class ThreadLocalStatsT<TLStatsNoLocking>;
template class TLStatT<TLStatsNoLocking>;
template class TLTimeseriesT<TLStatsNoLocking>;
template class TLHistogramT<TLStatsNoLocking>;
template class TLCounterT<TLStatsNoLocking>;

// Explicitly instantiate ThreadLocalStatsT and related classes
// when used with TLStatsThreadSafe.
template class ThreadLocalStatsT<TLStatsThreadSafe>;
template class TLStatT<TLStatsThreadSafe>;
template class TLTimeseriesT<TLStatsThreadSafe>;
template class TLHistogramT<TLStatsThreadSafe>;
template class TLCounterT<TLStatsThreadSafe>;

namespace detail {

bool shouldUpdateGlobalStatOnRead() {
  return !FLAGS_fb303_tcData_dont_update_on_read;
}

} // namespace detail

/// TLStatNameSet::Impl
///
/// Maintains the appearance of a set of shared-ptr<string> 's, indexed by the
/// string's. Only holds onto weak-ptr's internally.
class TLStatNameSet::Impl {
 private:
  using Sp = std::shared_ptr<std::string const>;
  using Wp = std::weak_ptr<std::string const>;

  struct SpDeleter {
    void operator()(std::string* const str) {
      instance().clean(*str);
      delete str;
    }
  };

  using Set = folly::F14FastMap<std::string, Wp>;

  /// split to reduce potential lock contention at startup for threads looking
  /// up distinct names
  ///
  /// split by power-of-two to make bucketing slightly more efficient than
  /// otherwise, although note that the next thing done is to acquire a lock
  std::vector<folly::Synchronized<Set>> sets_;

  explicit Impl(size_t const spine) : sets_{folly::nextPowTwo(spine)} {}

  auto& set(std::string_view const name) {
    auto const mask = sets_.size() - 1;
    auto const hash = std::hash<std::string_view>{}(name);
    return sets_[hash & mask];
  }

  void clean(std::string const& name) {
    auto const wlock = set(name).wlock();
    if (auto const it = wlock->find(name); it != wlock->end()) {
      if (!it->second.lock()) {
        wlock->erase(it);
      }
    }
  }

  Sp getSlow(std::string_view const name) {
    auto const wlock = set(name).wlock();
    if (auto const it = wlock->find(name); it != wlock->end()) {
      if (auto sp = it->second.lock()) {
        return sp;
      }
    }
    auto sp = Sp{new std::string(name), SpDeleter{}};
    (*wlock)[std::string(name)] = sp;
    return sp;
  }

  Sp getFast(std::string_view const name) {
    auto const rlock = set(name).rlock();
    if (auto const it = rlock->find(name); it != rlock->end()) {
      if (auto sp = it->second.lock()) {
        return sp;
      }
    }
    return nullptr;
  }

 public:
  Sp get(std::string_view const name) {
    auto sp = getFast(name);
    return sp ? sp : getSlow(name);
  }

  static Impl& instance() {
    static auto& ref = *new Impl(std::thread::hardware_concurrency());
    return ref;
  }
};

std::shared_ptr<std::string const> TLStatNameSet::get(
    std::string_view const name) {
  return Impl::instance().get(name);
}

} // namespace facebook::fb303
