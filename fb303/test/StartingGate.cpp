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
#include <fb303/test/StartingGate.h>

namespace facebook {
namespace fb303 {

StartingGate::StartingGate(size_t threadCount) : totalThreads_{threadCount} {}

void StartingGate::wait() {
  std::unique_lock lock{mutex_};
  ++waitingThreads_;
  cv_.notify_all();
  cv_.wait(lock, [&] { return ready_; });
}

void StartingGate::waitForWaitingThreads() {
  std::unique_lock lock{mutex_};
  cv_.wait(lock, [&] { return waitingThreads_ == totalThreads_; });
}

void StartingGate::open() {
  std::unique_lock lock{mutex_};
  ready_ = true;
  cv_.notify_all();
}

} // namespace fb303
} // namespace facebook
