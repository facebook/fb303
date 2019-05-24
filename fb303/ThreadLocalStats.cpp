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
#include <fb303/ThreadLocalStats.h>
#include <fb303/ThreadLocalStats-defs.h>

namespace facebook {
namespace fb303 {

/*
 * Explicitly instantiate the commonly-used instantations of ThreadLocalStatsT.
 * This way most users never need to include ThreadLocalStats-defs.h, which
 * helps speed up the build.
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

template class ThreadLocalStatsT<TLStatsWithSharedMutex>;
template class TLStatT<TLStatsWithSharedMutex>;
template class TLTimeseriesT<TLStatsWithSharedMutex>;
template class TLHistogramT<TLStatsWithSharedMutex>;
template class TLCounterT<TLStatsWithSharedMutex>;

} // namespace fb303
} // namespace facebook
