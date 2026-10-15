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

#pragma once

#include <mutex>

namespace facebook::fb303::detail {

/*
 * A mutex whose shared-lock interface aliases its exclusive lock, so
 * folly::Synchronized exposes its shared-lock API while every lock is backed
 * by a plain std::mutex.
 */
class ExclusiveStatMutex {
 public:
  void lock() {
    mutex_.lock();
  }
  void unlock() {
    mutex_.unlock();
  }
  bool try_lock() {
    return mutex_.try_lock();
  }
  void lock_shared() {
    mutex_.lock();
  }
  void unlock_shared() {
    mutex_.unlock();
  }
  bool try_lock_shared() {
    return mutex_.try_lock();
  }

 private:
  std::mutex mutex_;
};

} // namespace facebook::fb303::detail
