#include "rbase.h"

// This implementation is based on of Jeff Preshing's "lightweight semaphore"
// https://github.com/preshing/cpp11-on-multicore/blob/master/common/sema.h
// zlib license:
//
// Copyright (c) 2015 Jeff Preshing
//
// This software is provided 'as-is', without any express or implied
// warranty. In no event will the authors be held liable for any damages
// arising from the use of this software.
//
// Permission is granted to anyone to use this software for any purpose,
// including commercial applications, and to alter it and redistribute it
// freely, subject to the following restrictions:
//
// 1. The origin of this software must not be misrepresented; you must not
//  claim that you wrote the original software. If you use this software
//  in a product, an acknowledgement in the product documentation would be
//  appreciated but is not required.
// 2. Altered source versions must be plainly marked as such, and must not be
//  misrepresented as being the original software.
// 3. This notice may not be removed or altered from any source distribution.

//#define USE_UNIX_SEMA

#if defined(_WIN32) && !defined(USE_UNIX_SEMA)
  #include <windows.h>
  #undef min
  #undef max
#elif defined(__MACH__) && !defined(USE_UNIX_SEMA)
  #undef panic // mach/mach.h defines a function called panic()
  #include <mach/mach.h>
  // redefine panic
  #define panic(fmt, ...) _panic(__FILE__, __LINE__, __FUNCTION__, fmt, ##__VA_ARGS__)
#elif defined(__unix__) || defined(USE_UNIX_SEMA)
  #include <semaphore.h>
#else
  #error Unsupported platform
#endif

ASSUME_NONNULL_BEGIN

#define USECS_IN_1_SEC 1000000
#define NSECS_IN_1_SEC 1000000000


//---------------------------------------------------------------------------------------------
#if defined(_WIN32) && !defined(USE_UNIX_SEMA)

bool SemaInit(Sema* sp, u32 initcount) {
  assert(initcount <= 0x7fffffff);
  *sp = (Sema)CreateSemaphoreW(NULL, (int)initcount, 0x7fffffff, NULL);
  return *sp != NULL;
}

void SemaDispose(Sema* sp) {
  CloseHandle(*sp);
}

bool SemaWait(Sema* sp) {
  const unsigned long infinite = 0xffffffff;
  return WaitForSingleObject(*sp, infinite) == 0;
}

bool SemaTryWait(Sema* sp) {
  return WaitForSingleObject(*sp, 0) == 0;
}

bool SemaTimedWait(Sema* sp, u64 timeout_usecs) {
  return WaitForSingleObject(*sp, (unsigned long)(timeout_usecs / 1000)) == 0;
}

bool SemaSignal(Sema* sp, u32 count) {
  assert(count > 0);
  // while (!ReleaseSemaphore(*sp, count, NULL)) {
  // }
  return ReleaseSemaphore(*sp, count, NULL);
}

//---------------------------------------------------------------------------------------------
#elif defined(__MACH__) && !defined(USE_UNIX_SEMA)
// Can't use POSIX semaphores due to
// https://web.archive.org/web/20140109214515/
// http://lists.apple.com/archives/darwin-kernel/2009/Apr/msg00010.html

bool SemaInit(Sema* sp, u32 initcount) {
  assert(initcount <= 0x7fffffff);
  kern_return_t rc =
    semaphore_create(mach_task_self(), (semaphore_t*)sp, SYNC_POLICY_FIFO, (int)initcount);
  return rc == KERN_SUCCESS;
}

void SemaDispose(Sema* sp) {
  semaphore_destroy(mach_task_self(), *(semaphore_t*)sp);
}

bool SemaWait(Sema* sp) {
  semaphore_t s = *(semaphore_t*)sp;
  while (1) {
    kern_return_t rc = semaphore_wait(s);
    if (rc != KERN_ABORTED)
      return rc == KERN_SUCCESS;
  }
}

bool SemaTryWait(Sema* sp) {
  return SemaTimedWait(sp, 0);
}

bool SemaTimedWait(Sema* sp, u64 timeout_usecs) {
  mach_timespec_t ts;
  ts.tv_sec = (u32)(timeout_usecs / USECS_IN_1_SEC);
  ts.tv_nsec = (int)((timeout_usecs % USECS_IN_1_SEC) * 1000);
  // Note:
  // semaphore_wait_deadline was introduced in macOS 10.6
  // semaphore_timedwait was introduced in macOS 10.10
  // https://developer.apple.com/library/prerelease/mac/documentation/General/Reference/
  //   APIDiffsMacOSX10_10SeedDiff/modules/Darwin.html
  semaphore_t s = *(semaphore_t*)sp;
  while (1) {
    kern_return_t rc = semaphore_timedwait(s, ts);
    if (rc != KERN_ABORTED)
      return rc == KERN_SUCCESS;
    // TODO: update ts; subtract time already waited and retry (loop).
    // For now, let's just return with an error:
    return false;
  }
}

bool SemaSignal(Sema* sp, u32 count) {
  assert(count > 0);
  semaphore_t s = *(semaphore_t*)sp;
  kern_return_t rc = 0; // KERN_SUCCESS
  while (count-- > 0) {
    rc += semaphore_signal(s); // == ...
    // auto rc1 = semaphore_signal(s);
    // if (rc1 != KERN_SUCCESS) {
    //   rc = rc1;
    // }
  }
  return rc == KERN_SUCCESS;
}

//---------------------------------------------------------------------------------------------
#elif defined(__unix__) || defined(USE_UNIX_SEMA)

// TODO: implementation based on futex (for Linux and OpenBSD). See "__TBB_USE_FUTEX" of oneTBB

bool SemaInit(Sema* sp, u32 initcount) {
  return sem_init((sem_t*)sp, 0, initcount) == 0;
}

void SemaDispose(Sema* sp) {
  sem_destroy((sem_t*)sp);
}

bool SemaWait(Sema* sp) {
  // http://stackoverflow.com/questions/2013181/gdb-causes-sem-wait-to-fail-with-eintr-error
  int rc;
  do {
    rc = sem_wait((sem_t*)sp);
  } while (rc == -1 && errno == EINTR);
  return rc == 0;
}

bool SemaTryWait(Sema* sp) {
  int rc;
  do {
    rc = sem_trywait((sem_t*)sp);
  } while (rc == -1 && errno == EINTR);
  return rc == 0;
}

bool SemaTimedWait(Sema* sp, u64 timeout_usecs) {
  struct timespec ts;
  clock_gettime(CLOCK_REALTIME, &ts);
  ts.tv_sec += (time_t)(timeout_usecs / USECS_IN_1_SEC);
  ts.tv_nsec += (long)(timeout_usecs % USECS_IN_1_SEC) * 1000;
  // sem_timedwait bombs if you have more than 1e9 in tv_nsec
  // so we have to clean things up before passing it in
  if (ts.tv_nsec >= NSECS_IN_1_SEC) {
    ts.tv_nsec -= NSECS_IN_1_SEC;
    ++ts.tv_sec;
  }
  int rc;
  do {
    rc = sem_timedwait((sem_t*)sp, &ts);
  } while (rc == -1 && errno == EINTR);
  return rc == 0;
}

bool SemaSignal(Sema* sp, u32 count) {
  assert(count > 0);
  while (count-- > 0) {
    while (sem_post((sem_t*)sp) == -1) {
      return false;
    }
  }
  return true;
}

//---------------------------------------------------------------------------------------------
#endif /* system */
// end of Sema implementations

//---------------------------------------------------------------------------------------------
// LSema

// LSEMA_MAX_SPINS is the upper limit of how many times to retry a CAS while spinning.
// After LSEMA_MAX_SPINS CAS attempts has failed (not gotten a signal), the implementation
// falls back on calling SemaWait.
//
// The number 10000 has been choosen by looking at contention between a few threads competing
// for signal & wait on macOS 10.15 x86_64. In most observed cases two threads with zero overhead
// racing to wait usually spends around 200–3000 loop cycles before succeeding. (clang -O0)
//
#define LSEMA_MAX_SPINS 10000


static bool _LSemaWaitPartialSpin(LSema* s, u64 timeout_usecs) {
  ssize_t oldCount;
  int spin = LSEMA_MAX_SPINS;
  while (--spin >= 0) {
    oldCount = AtomicLoad(&s->count);
    if (oldCount > 0 && AtomicCASAcqRel(&s->count, &oldCount, oldCount - 1))
      return true;
    // Prevent the compiler from collapsing the loop
    // [rsms]: Is this really needed? Find out. I think both clang and gcc will avoid
    //         messing with loops that contain atomic ops,
    //__asm__ volatile("" ::: "memory");
    atomic_signal_fence(memory_order_acquire);
  }
  oldCount = atomic_fetch_sub_explicit(&s->count, 1, memory_order_acquire);
  if (oldCount > 0)
    return true;
  if (timeout_usecs == 0) {
    if (SemaWait(&s->sema))
      return true;
  }
  if (timeout_usecs > 0 && SemaTimedWait(&s->sema, timeout_usecs))
    return true;
  // At this point, we've timed out waiting for the semaphore, but the
  // count is still decremented indicating we may still be waiting on
  // it. So we have to re-adjust the count, but only if the semaphore
  // wasn't signaled enough times for us too since then. If it was, we
  // need to release the semaphore too.
  while (1) {
    oldCount = AtomicLoadAcq(&s->count);
    if (oldCount >= 0 && SemaTryWait(&s->sema))
      return true;
    if (oldCount < 0 && AtomicCAS(&s->count, &oldCount, oldCount + 1))
      return false;
  }
}


bool LSemaInit(LSema* s, u32 initcount) {
  s->count = ATOMIC_VAR_INIT(initcount);
  return SemaInit(&s->sema, initcount);
}

void LSemaDispose(LSema* s) {
  SemaDispose(&s->sema);
}

bool LSemaWait(LSema* s) {
  return LSemaTryWait(s) || _LSemaWaitPartialSpin(s, 0);
}

bool LSemaTryWait(LSema* s) {
  ssize_t oldCount = AtomicLoad(&s->count);
  while (oldCount > 0) {
    if (atomic_compare_exchange_weak_explicit(
         &s->count, &oldCount, oldCount - 1, memory_order_acquire, memory_order_relaxed))
    {
      return true;
    }
  }
  return false;
}

bool LSemaTimedWait(LSema* s, u64 timeout_usecs) {
  return LSemaTryWait(s) || _LSemaWaitPartialSpin(s, timeout_usecs);
}

void LSemaSignal(LSema* s, u32 count) {
  assert(count > 0);
  ssize_t oldCount = atomic_fetch_add_explicit(&s->count, (ssize_t)count, memory_order_release);
  ssize_t toRelease = -oldCount < count ? -oldCount : (ssize_t)count;
  if (toRelease > 0)
    SemaSignal(&s->sema, (u32)toRelease);
}

size_t LSemaApproxAvail(LSema* s) {
  ssize_t count = AtomicLoad(&s->count);
  return count > 0 ? (size_t)(count) : 0;
}


//---------------------------------------------------------------------------------------------
#ifdef R_TESTING_ENABLED


typedef struct TestThread {
  thrd_t t;
  u32    id;
  Sema*  sema;     // the shared semaphore (if NULL, use lsema)
  LSema* lsema;    // the shared light-weight "spinning" semaphore
  u32    nsignals; // number of times to signal with the semaphore
} TestThread;


static int test_thread(void* tptr) {
  auto t = (TestThread*)tptr;
  for (u32 i = 0; i < t->nsignals; i++) {
    // add some thread scheduling jitter by sleeping up to 10 microseconds
    usleep(rand() % 10);
    //dlog("T %u SemaSignal(1)", t->id);
    if (t->sema != NULL) {
      SemaSignal(t->sema, 1);
    } else {
      LSemaSignal(t->lsema, 1);
    }
  }
  return 0;
}


static void test_sema(bool use_lsema) {
  TestThread threads[8]; // threads to spawn, each which will signal the semaphore
  u32 nsignals = 8;      // number of times each thread should signal the semaphore

  // create semaphore
  //dlog("using %s implementation", use_lsema ? "LSema" : "Sema");
  Sema  sema;
  LSema lsema;
  if (use_lsema) {
    assert(LSemaInit(&lsema, 0));
  } else {
    assert(SemaInit(&sema, 0));
  }

  // init & spawn threads
  //dlog("spawning %u threads", (u32)countof(threads));
  for (u32 i = 0; i < countof(threads); i++) {
    TestThread* t = &threads[i];
    t->id = i + 1;
    if (use_lsema) {
      t->sema = NULL;
      t->lsema = &lsema;
    } else {
      t->sema = &sema;
      t->lsema = NULL;
    }
    t->nsignals = nsignals;
    auto status = thrd_create(&t->t, test_thread, t);
    asserteq(status, thrd_success);
  }

  // wait for every signal
  u64 timeout_usecs = 200 * 1000; // 200ms
  for (u32 i = 0; i < countof(threads) * nsignals; i++) {
    if (use_lsema) {
      assertf(LSemaTimedWait(&lsema, timeout_usecs), "timeout");
    } else {
      assertf(SemaTimedWait(&sema, timeout_usecs), "timeout");
    }
  }

  // make sure there's no left over extra signal
  if (use_lsema) {
    assert(LSemaTryWait(&lsema) == false); // should fail
  } else {
    assert(SemaTryWait(&sema) == false); // should fail
  }

  // wait for threads
  for (u32 i = 0; i < countof(threads); i++) {
    auto t = &threads[i];
    int retval;
    thrd_join(t->t, &retval);
  }

  if (use_lsema) {
    LSemaDispose(&lsema);
  } else {
    SemaDispose(&sema);
  }
}


R_TEST(thread_sema) {
  test_sema(/*use_lsema*/false);
}

R_TEST(thread_lsema) {
  test_sema(/*use_lsema*/true);
}


#endif /*R_TESTING_ENABLED*/
ASSUME_NONNULL_END
