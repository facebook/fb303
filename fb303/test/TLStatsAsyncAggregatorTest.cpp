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

#include <fb303/TLStatsAsyncAggregator.h>
#include <folly/io/async/EventBase.h>

#include <gtest/gtest.h>

using namespace std;
using namespace facebook;
using namespace facebook::fb303;
using namespace folly;

TEST(TLStatsAsyncAggregatorTest, NoRequestContextLeak) {
  // Setup fields
  ThreadLocalStatsT<TLStatsNoLocking> tlstats{};
  TLStatsAsyncAggregator aggregator{&tlstats};
  EventBase eventBase;

  // Weak pointer to attach to request context
  std::weak_ptr<RequestContext> weakContext;
  {
    // Guard to clean up RequestContext in this scope.
    RequestContextScopeGuard guard;
    weakContext = RequestContext::saveContext();
    aggregator.scheduleAggregation(&eventBase);
  }

  // Since we are not carrying request context anymore,
  // the RequestContext gets cleared up by guard,
  // and no other reference is alive, so nullptr is expected.

  EXPECT_TRUE(weakContext.expired());
}
