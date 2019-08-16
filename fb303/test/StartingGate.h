/*
 * Copyright 2019-present Facebook, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#pragma once

#include <condition_variable>
#include <mutex>

namespace facebook {
namespace fb303 {

/**
 * For more accurate benchmarking and race condition testing,
 * StaringGate allows a test to wait until all threads are ready to
 * go and then wake them all.
 *
 * TODO: This looks a lot like C++20's std::barrier. Since we intend
 * to support C++14 for now, we could implement a folly::barrier and
 * replace uses of StartingGate with it.
 */
class StartingGate {
 public:
  /**
   * Number of threads that will call wait() must be specified up front.
   */
  explicit StartingGate(size_t threadCount);

  /**
   * Called by thread and waits until open() is called.
   */
  void wait();

  /**
   * Waits until all threads have called wait(). Useful for removing thread
   * setup from benchmark time when using folly benchmark.
   */
  void waitForWaitingThreads();

  /**
   * Allows all threads to proceed.
   */
  void open();

  void waitThenOpen() {
    waitForWaitingThreads();
    open();
  }

 private:
  std::mutex mutex_;
  std::condition_variable cv_;
  size_t waitingThreads_{0};
  bool ready_{false};
  const size_t totalThreads_;
};

} // namespace fb303
} // namespace facebook
