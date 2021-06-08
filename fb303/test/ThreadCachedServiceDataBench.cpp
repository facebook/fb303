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

#include <fb303/ThreadCachedServiceData.h>

#include <folly/Benchmark.h>
#include <folly/init/Init.h>

BENCHMARK(FormattedKeyHolderOld, iters) {
  folly::BenchmarkSuspender braces;
  auto rep = [](auto s, auto n) {
    std::string r;
    while (n--) {
      r += s;
    }
    return r;
  };
  DEFINE_dynamic_timeseries(obj, "key_holder_bench_{}_{}_{}_{}");
  auto key0 = std::array{rep("ant", 5), rep("cat", 7), rep("eel", 3)};
  constexpr auto key1 = std::array{2, 3, 5, 7, 11, 13, 17};
  constexpr auto key2 =
      std::array<std::string_view, 5>{"fish", "wolf", "bear", "seal", "mole"};
  constexpr auto key3 = std::array{
      1ll << 25,
      1ll << 47,
      1ll << 13,
      1ll << 23,
      1ll << 61,
      1ll << 3,
      1ll << 57,
      1ll << 22,
      1ll << 41,
      1ll << 19,
      1ll << 31};
  constexpr auto size = key0.size() * key1.size() * key2.size() * key3.size();
  static_assert(size == 1155);
  for (size_t i = 0; i < size; ++i) {
    STATS_obj.add(
        1,
        key0[i % key0.size()],
        key1[i % key1.size()],
        key2[i % key2.size()],
        key3[i % key3.size()]);
  }
  braces.dismissing([&] {
    while (iters--) {
      size_t i = iters - 1;
      STATS_obj.add(
          1,
          key0[i % key0.size()],
          key1[i % key1.size()],
          key2[i % key2.size()],
          key3[i % key3.size()]);
    }
  });
}

int main(int argc, char** argv) {
  folly::Init init(&argc, &argv);
  folly::runBenchmarks();
  return 0;
}
