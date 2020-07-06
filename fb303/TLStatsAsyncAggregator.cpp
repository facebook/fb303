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

#include <fb303/TLStatsAsyncAggregator.h>

using folly::EventBase;

namespace facebook {
namespace fb303 {

const uint32_t TLStatsAsyncAggregator::kDefaultIntervalMS;

/*
 * Create a new TLStatsAsyncAggregator.
 *
 */
TLStatsAsyncAggregator::TLStatsAsyncAggregator(ThreadLocalStats* stats)
    : intervalMS_(kDefaultIntervalMS), stats_(stats) {}

void TLStatsAsyncAggregator::scheduleAggregation(
    EventBase* eventBase,
    uint32_t intervalMS) {
  // Note that we install the AsyncTimeout as an INTERNAL event.
  // This way our timeout won't prevent EventBase::loop() if there are no
  // other events to process besides our timeout.  Stats aggregation by itself
  // generally isn't the main purpose of an event loop, and we don't need to
  // keep going just to aggregate stats if there is nothing else left running
  // that can generate stats.
  attachEventBase(eventBase, folly::AsyncTimeout::InternalEnum::INTERNAL);
  intervalMS_ = intervalMS;
  scheduleTimeout(intervalMS_);
}

void TLStatsAsyncAggregator::timeoutExpired() noexcept {
  stats_->aggregate();
  scheduleTimeout(intervalMS_);
}

} // namespace fb303
} // namespace facebook
