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

#include <fb303/QuantileStat.h>

#include <gtest/gtest.h>

using namespace facebook::fb303;

struct MockClock {
 public:
  using duration = std::chrono::steady_clock::duration;
  using time_point = std::chrono::steady_clock::time_point;

  static time_point now() {
    return Now;
  }

  static time_point Now;
};

MockClock::time_point MockClock::Now = MockClock::time_point{};

const std::vector<std::pair<std::chrono::seconds, size_t>> slidingWindowDefs = {
    {std::chrono::seconds{1}, 60},
    {std::chrono::seconds{10}, 60},
    {std::chrono::seconds{60}, 60}};

const std::vector<double> quantiles = {.95, .99, .999, 1.0};

class QuantileStatTest : public ::testing::Test {
 public:
  QuantileStatTest() : stat(slidingWindowDefs) {}

  BasicQuantileStat<MockClock> stat;

  void SetUp() override {
    MockClock::Now = MockClock::time_point{};
  }
};

TEST_F(QuantileStatTest, AllEstimatesReported) {
  auto estimates = stat.getEstimates(quantiles);

  EXPECT_EQ(4, estimates.allTimeEstimate.quantiles.size());
  EXPECT_EQ(.95, estimates.allTimeEstimate.quantiles[0].first);
  EXPECT_EQ(.99, estimates.allTimeEstimate.quantiles[1].first);
  EXPECT_EQ(.999, estimates.allTimeEstimate.quantiles[2].first);
  EXPECT_EQ(1.0, estimates.allTimeEstimate.quantiles[3].first);

  EXPECT_EQ(3, estimates.slidingWindows.size());

  EXPECT_EQ(std::chrono::seconds{1}, estimates.slidingWindows[0].windowLength);
  EXPECT_EQ(60, estimates.slidingWindows[0].nWindows);
  EXPECT_EQ(4, estimates.slidingWindows[0].estimate.quantiles.size());
  EXPECT_EQ(.95, estimates.slidingWindows[0].estimate.quantiles[0].first);
  EXPECT_EQ(.99, estimates.slidingWindows[0].estimate.quantiles[1].first);
  EXPECT_EQ(.999, estimates.slidingWindows[0].estimate.quantiles[2].first);
  EXPECT_EQ(1.0, estimates.slidingWindows[1].estimate.quantiles[3].first);

  EXPECT_EQ(std::chrono::seconds{10}, estimates.slidingWindows[1].windowLength);
  EXPECT_EQ(60, estimates.slidingWindows[1].nWindows);
  EXPECT_EQ(4, estimates.slidingWindows[1].estimate.quantiles.size());
  EXPECT_EQ(.95, estimates.slidingWindows[1].estimate.quantiles[0].first);
  EXPECT_EQ(.99, estimates.slidingWindows[1].estimate.quantiles[1].first);
  EXPECT_EQ(.999, estimates.slidingWindows[1].estimate.quantiles[2].first);
  EXPECT_EQ(1.0, estimates.slidingWindows[1].estimate.quantiles[3].first);

  EXPECT_EQ(std::chrono::seconds{60}, estimates.slidingWindows[2].windowLength);
  EXPECT_EQ(60, estimates.slidingWindows[2].nWindows);
  EXPECT_EQ(4, estimates.slidingWindows[2].estimate.quantiles.size());
  EXPECT_EQ(.95, estimates.slidingWindows[2].estimate.quantiles[0].first);
  EXPECT_EQ(.99, estimates.slidingWindows[2].estimate.quantiles[1].first);
  EXPECT_EQ(.999, estimates.slidingWindows[2].estimate.quantiles[2].first);
  EXPECT_EQ(1.0, estimates.slidingWindows[1].estimate.quantiles[3].first);
}

TEST_F(QuantileStatTest, getEstimate) {
  for (int i = 1; i <= 100; ++i) {
    stat.addValue(i);
  }
  // Flush the buffer.
  MockClock::Now += std::chrono::seconds{1};
  auto estimates = stat.getEstimates(quantiles);

  EXPECT_EQ(5050, estimates.allTimeEstimate.sum);
  EXPECT_EQ(100, estimates.allTimeEstimate.count);
  EXPECT_EQ(95.5, estimates.allTimeEstimate.quantiles[0].second);
  EXPECT_EQ(100, estimates.allTimeEstimate.quantiles[3].second);

  for (const auto& slidingWindow : estimates.slidingWindows) {
    EXPECT_EQ(5050, slidingWindow.estimate.sum);
    EXPECT_EQ(100, slidingWindow.estimate.count);
    EXPECT_EQ(95.5, slidingWindow.estimate.quantiles[0].second);
    EXPECT_EQ(100, slidingWindow.estimate.quantiles[3].second);
  }
}

TEST_F(QuantileStatTest, SlidingWindows) {
  for (int i = 1; i <= 100; ++i) {
    stat.addValue(i);
  }

  // Advance the buffer so that the 60s counter has no values left
  MockClock::Now += std::chrono::seconds{61};
  auto estimates = stat.getEstimates(quantiles);

  EXPECT_EQ(5050, estimates.allTimeEstimate.sum);
  EXPECT_EQ(100, estimates.allTimeEstimate.count);
  EXPECT_EQ(95.5, estimates.allTimeEstimate.quantiles[0].second);
  EXPECT_EQ(100, estimates.allTimeEstimate.quantiles[3].second);

  EXPECT_EQ(0, estimates.slidingWindows[0].estimate.sum);
  EXPECT_EQ(0, estimates.slidingWindows[0].estimate.count);
  EXPECT_EQ(0, estimates.slidingWindows[0].estimate.quantiles[0].second);
  EXPECT_EQ(0, estimates.slidingWindows[0].estimate.quantiles[3].second);

  EXPECT_EQ(5050, estimates.slidingWindows[1].estimate.sum);
  EXPECT_EQ(100, estimates.slidingWindows[1].estimate.count);
  EXPECT_EQ(95.5, estimates.slidingWindows[1].estimate.quantiles[0].second);
  EXPECT_EQ(100, estimates.slidingWindows[1].estimate.quantiles[3].second);

  EXPECT_EQ(5050, estimates.slidingWindows[2].estimate.sum);
  EXPECT_EQ(100, estimates.slidingWindows[2].estimate.count);
  EXPECT_EQ(95.5, estimates.slidingWindows[2].estimate.quantiles[0].second);
  EXPECT_EQ(100, estimates.slidingWindows[1].estimate.quantiles[3].second);
}

TEST_F(QuantileStatTest, BufferFlush) {
  for (int i = 1; i <= 100; ++i) {
    stat.addValue(i);
  }
  stat.flush();
  auto estimates = stat.getEstimates(quantiles);

  EXPECT_EQ(5050, estimates.allTimeEstimate.sum);
  EXPECT_EQ(100, estimates.allTimeEstimate.count);
  EXPECT_EQ(95.5, estimates.allTimeEstimate.quantiles[0].second);
  EXPECT_EQ(100, estimates.allTimeEstimate.quantiles[3].second);

  for (const auto& slidingWindow : estimates.slidingWindows) {
    EXPECT_EQ(5050, slidingWindow.estimate.sum);
    EXPECT_EQ(100, slidingWindow.estimate.count);
    EXPECT_EQ(95.5, slidingWindow.estimate.quantiles[0].second);
    EXPECT_EQ(100, slidingWindow.estimate.quantiles[3].second);
  }
}
