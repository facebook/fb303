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

#include <fb303/SynchMap.h>
#include <gtest/gtest.h>
#include <thrift/lib/cpp/concurrency/PosixThreadFactory.h>
#include <thrift/lib/cpp/concurrency/ThreadManager.h>
#include <time.h>
#include "common/base/Random.h"

using namespace apache::thrift::concurrency;
using namespace facebook;
using std::shared_ptr;

DEFINE_int32(task_time, 10, "");
DEFINE_int32(num_threads, 20, "");

typedef fb303::SynchMap<int64_t, int64_t> SynchMapInt64;

// ------------------------------------------------------------
// TEST: SynchMapBasic
// ------------------------------------------------------------

TEST(SynchMapTest, SynchMapBasic) {
  SynchMapInt64 map;
  shared_ptr<int64_t> res;

  // Note: The res.reset() statements below are necessary to release the lock
  //  on the value returned; otherwise, the value is still locked for the
  //  lifetime of the pointer, and subsequent 'get()' calls will fail.
  //
  // Alternately, we could create a miniscope

  EXPECT_FALSE(map.contains(0));
  EXPECT_EQ(res = map.get(0), nullptr);
  res.reset();

  map.set(0, 10);

  EXPECT_TRUE(map.contains(0));
  EXPECT_NE(res = map.get(0), nullptr);
  EXPECT_EQ(*res, 10);
  res.reset();

  EXPECT_FALSE(map.contains(1));
  EXPECT_EQ(res = map.get(1), nullptr);
  res.reset();

  map.set(1, 99);

  EXPECT_TRUE(map.contains(1));
  EXPECT_NE(res = map.get(1), nullptr);
  EXPECT_EQ(*res, 99);
  res.reset();

  EXPECT_FALSE(map.contains(2));
  EXPECT_EQ(res = map.get(2), nullptr);
  res.reset();

  map.set(2, 1000);

  EXPECT_TRUE(map.contains(2));
  EXPECT_NE(res = map.get(2), nullptr);
  EXPECT_EQ(*res, 1000);
  res.reset();

  EXPECT_NE(res = map.get(0), nullptr);
  EXPECT_EQ(*res, 10);
  res.reset();

  map.set(0, 15);

  EXPECT_TRUE(map.contains(0));
  EXPECT_NE(res = map.get(0), nullptr);
  EXPECT_EQ(*res, 15);
  res.reset();

  EXPECT_FALSE(map.contains(3));
  EXPECT_NE(res = map.getOrCreate(3, 2000), nullptr);
  EXPECT_EQ(*res, 2000);
  res.reset();

  EXPECT_TRUE(map.contains(3));
  EXPECT_NE(res = map.getOrCreate(3, 5000), nullptr);
  EXPECT_EQ(*res, 2000);
  res.reset();

  // Test erase when item is not checked out
  EXPECT_TRUE(map.erase(3));
  EXPECT_FALSE(map.contains(3));
  EXPECT_EQ(res = map.get(3), nullptr);
  res.reset();

  // Test when item is checked out
  EXPECT_NE(res = map.get(0), nullptr);
  EXPECT_EQ(*res, 15);
  EXPECT_TRUE(map.erase(0));
  // verify we can still access the value
  EXPECT_EQ(*res, 15);
  EXPECT_FALSE(map.contains(0));
  EXPECT_EQ(res = map.get(0), nullptr); // also resets res
  res.reset();

  // Test when an unlocked item is deleted
  map.set(0, 43);
  EXPECT_NE(res = map.get(0), nullptr);
  EXPECT_EQ(*res, 43);
  res.reset();
  {
    SynchMapInt64::LockAndItem lItem = map.getUnlocked(0);
    map.erase(0);
    EXPECT_EQ(res = map.get(0), nullptr);
    // verify we can still access the value
    int64_t value;
    lItem.first->lock();
    value = *(lItem.second);
    lItem.first->unlock();
    EXPECT_EQ(value, 43);
  }
  EXPECT_FALSE(map.contains(0));
}

class SynchMapTask : public Runnable {
 public:
  SynchMapTask(SynchMapInt64* map, int id)
      : map_(map),
        id_(id),
        random_(
            std::chrono::system_clock::now().time_since_epoch().count() ^
            getpid()) {}
  void run() override {
    int64_t start = ::time(nullptr);
    int64_t now = start;
    for (now = start; (now - start) < FLAGS_task_time; now = ::time(nullptr)) {
      const int kNumOps = 10000;
      for (int i = 0; i < kNumOps; ++i) {
        UniformInt32 keyRange(1, 20);

        // this key will often match with those generated
        // by the other test threads, to produce collisions.
        const int64_t key = keyRange(random_);
        const int64_t val = 100 + i;

        // set a key in the map
        map_->set(key, val);
        // map should contain key -- value, however, may have already changed
        EXPECT_TRUE(map_->contains(key));

        {
          // now we lock the key for a while (150 us) and increment it
          // to simulate a longer-term operation on a key
          SynchMapInt64::LockedValuePtr res = map_->get(key);
          const int64_t oldVal = *res;
          usleep(150);
          *res += 1;

          // check that nobody else has modified it while we had it locked
          EXPECT_EQ(oldVal + 1, *res);
        }

        {
          // this key is guaranteed to be unique to this task, so it should
          // never collide -- that's why we can check that its value is what
          // we expected it to be.
          const int64_t uniqueKey = (uint64_t(id_ + 1) << 32) | key;
          map_->set(uniqueKey, val);
          shared_ptr<int64_t> res = map_->get(uniqueKey);
          EXPECT_NE(res, nullptr);
          EXPECT_EQ(*res, val);
        }
      }

      // ghetto-ass progress bar to show threads aren't stuck
      fputc('.', stderr);
      fflush(stderr);
    }
  }

 private:
  SynchMapInt64* map_;
  int id_;
  RandomInt32 random_;
};

// ------------------------------------------------------------
// TEST: SynchMapThreads
// ------------------------------------------------------------

TEST(SynchMapTest, SynchMapThreads) {
  const int kTasks = FLAGS_num_threads;
  if (!kTasks)
    return;

  shared_ptr<ThreadManager> mgr =
      ThreadManager::newSimpleThreadManager(FLAGS_num_threads);
  shared_ptr<ThreadFactory> factory(new PosixThreadFactory());
  mgr->threadFactory(factory);

  mgr->start();

  SynchMapInt64 map;
  for (int i = 0; i < kTasks; ++i) {
    mgr->add(shared_ptr<Runnable>(new SynchMapTask(&map, i)));
  }
  mgr->join();
  fputc('\n', stderr);
  EXPECT_EQ(mgr->pendingTaskCount(), 0);
}

// ------------------------------------------------------------
// TEST: SynchMapErase
// ------------------------------------------------------------

// The idea is Thread1 and Thread2 take a lock on an item, sleep
// for 2 sec and then whichever threads get the item first deletes it.

class SynchMapEraseTask : public Runnable {
 public:
  explicit SynchMapEraseTask(SynchMapInt64* map) : map_(map) {}

  void run() override {
    SynchMapInt64::LockedValuePtr ptr = map_->getOrCreate(1, 1);
    sleep(3);
    map_->erase(1);
  }

 private:
  SynchMapInt64* map_;
};

TEST(SynchMapTest, SynchMapErase) {
  SynchMapInt64 map;

  shared_ptr<ThreadManager> mgr =
      ThreadManager::newSimpleThreadManager(FLAGS_num_threads);
  shared_ptr<ThreadFactory> factory(new PosixThreadFactory());
  mgr->threadFactory(factory);

  mgr->start();
  mgr->add(shared_ptr<Runnable>(new SynchMapEraseTask(&map)));
  mgr->add(shared_ptr<Runnable>(new SynchMapEraseTask(&map)));
  mgr->join();
}
