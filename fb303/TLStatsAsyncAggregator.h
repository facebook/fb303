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

#include <fb303/ThreadLocalStats.h>
#include <folly/io/async/AsyncTimeout.h>
#include <folly/io/async/EventBase.h>

namespace facebook {
namespace fb303 {

/**
 * A class that uses AsyncTimeout to periodically call aggregate() on a
 * ThreadLocalStats object.
 */
class TLStatsAsyncAggregator : private folly::AsyncTimeout {
 public:
  /*
   * For now, we only support TLStatsNoLocking.
   * Users using TLStatsThreadSafe can easily use a separate a non-async thread
   * to perform the aggregation.  We could always turn TLStatsAsyncAggregator
   * into a template as well to support TLStatsThreadSafe in the future if
   * there is a need to do so.
   */
  typedef ThreadLocalStatsT<TLStatsNoLocking> ThreadLocalStats;

  /**
   * By default, scheduleAggregation() will cause aggregate() to be called
   * once every second.
   */
  static const uint32_t kDefaultIntervalMS = 1000;

  /**
   * Create a new TLStatsAsyncAggregator.
   *
   * Note that the TLStatsAsyncAggregator just points at the ThreadLocalStats
   * object, it does not assume ownership.  The user is responsible for
   * ensuring that the TLStatsAsyncAggregator is destroyed before the
   * ThreadLocalStats object.
   */
  explicit TLStatsAsyncAggregator(ThreadLocalStats* stats);

  /**
   * Schedule aggregate to be called automatically every intervalMS
   * milliseconds.
   */
  void scheduleAggregation(
      folly::EventBase* eventBase,
      uint32_t intervalMS = kDefaultIntervalMS);

  void stopAggregation() {
    cancelTimeout();
  }

 private:
  // Forbidden copy constructor and assignment operator
  TLStatsAsyncAggregator(const TLStatsAsyncAggregator&) = delete;
  TLStatsAsyncAggregator& operator=(const TLStatsAsyncAggregator&) = delete;

  // AsyncTimeout methods
  void timeoutExpired() noexcept override;

  uint32_t intervalMS_;
  ThreadLocalStats* stats_;
};

} // namespace fb303
} // namespace facebook
