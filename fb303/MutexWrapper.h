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

#include <folly/SharedMutex.h>
#include <folly/Synchronized.h>

namespace facebook {
namespace fb303 {

// Stats objects need a small footprint fast mutex. std::mutex is based on
// pthread_mutex and 40 bytes in size. folly::SharedMutex is not as performance
// as std::mutex under heavy contention but is only 4 bytes in size.
// DistributedMutex, once supported by folly::Synchronized might be the ideal
// choice. For now just a wrapper around folly::SharedMutex. The locks are used
// via folly::Synchronized which by default generates different APIs for
// exclusive mode when underlying lock is mutex vs. RW lock. The wrapper also
// has the possibility of being used with folly::DistributedMutex. We could
// wrap that here by tracking the token returned by its lock in a TLS array.
class MutexWrapper {
 private:
  folly::SharedMutex mutex_;

 public:
  void lock() {
    mutex_.lock();
  }

  void unlock() {
    mutex_.unlock();
  }
};

} // namespace fb303
} // namespace facebook
