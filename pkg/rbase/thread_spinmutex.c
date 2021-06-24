#include "rbase.h"

ASSUME_NONNULL_BEGIN

// The value of kYieldProcessorTries is cargo culted from TCMalloc, Windows
// critical section defaults, WebKit, etc.
#define kYieldProcessorTries 1000


void _spinMutexWait(SpinMutex* m) {
  while (1) {
    if (!atomic_exchange_explicit(&m->flag, true, memory_order_acquire))
      break;
    size_t n = kYieldProcessorTries;
    while (atomic_load_explicit(&m->flag, memory_order_relaxed)) {
      if (--n == 0) {
        // help the OS to reschedule threads
        n = kYieldProcessorTries;
        YIELD_THREAD();
      } else {
        // avoid starvation on hyper-threaded CPUs
        YIELD_CPU();
      }
    }
  }
}


void _hybridMutexWait(HybridMutex* m) {
  while (1) {
    if (!atomic_exchange_explicit(&m->flag, true, memory_order_acquire))
      break;
    size_t n = kYieldProcessorTries;
    while (atomic_load_explicit(&m->flag, memory_order_relaxed)) {
      if (--n == 0) {
        AtomicAdd(&m->nwait, 1);
        while (atomic_load_explicit(&m->flag, memory_order_relaxed)) {
          SemaWait(&m->sema);
        }
        AtomicSub(&m->nwait, 1);
      } else {
        // avoid starvation on hyper-threaded CPUs
        YIELD_CPU();
      }
    }
  }
}



// ————————————————————————————————————————————————————————————————————————————————————————
#ifdef R_TESTING_ENABLED

// -----------------------------------------
// SpinMutex

typedef struct SpinMutexTestThread {
  thrd_t     t;
  SpinMutex* lock;
  size_t     nlocks;
} SpinMutexTestThread;

static int spinmutex_test_thread(void* tptr) {
  auto t = (SpinMutexTestThread*)tptr;
  while (t->nlocks != 0) {
    SpinMutexLock(t->lock);
    //usleep(rand() % 10); // sleep up to 1ms
    SpinMutexUnlock(t->lock);
    t->nlocks--;
  }
  return 0;
}

R_TEST(thread_spin_mutex) {
  SpinMutex lock;
  SpinMutexInit(&lock);

  SpinMutexTestThread threads[10];
  size_t nlocks_per_thread = 100;

  for (u32 i = 0; i < countof(threads); i++) {
    SpinMutexTestThread* t = &threads[i];
    t->lock = &lock;
    t->nlocks = nlocks_per_thread;
    auto status = thrd_create(&t->t, spinmutex_test_thread, t);
    asserteq(status, thrd_success);
  }

  for (u32 i = 0; i < countof(threads); i++) {
    auto t = &threads[i];
    int retval;
    thrd_join(t->t, &retval);
    asserteq(t->nlocks, 0);
  }
}

// ------------------------------------
// HybridMutex

typedef struct HybridMutexTestThread {
  thrd_t       t;
  HybridMutex* lock;
  size_t       nlocks;
} HybridMutexTestThread;

static int hybridmutex_test_thread(void* tptr) {
  auto t = (HybridMutexTestThread*)tptr;
  while (t->nlocks != 0) {
    HybridMutexLock(t->lock);
    //usleep(rand() % 10); // sleep up to 1ms
    HybridMutexUnlock(t->lock);
    t->nlocks--;
  }
  return 0;
}

R_TEST(thread_hybrid_mutex) {
  HybridMutex lock;
  assert(HybridMutexInit(&lock));

  HybridMutexTestThread threads[10];
  size_t nlocks_per_thread = 100;

  for (u32 i = 0; i < countof(threads); i++) {
    HybridMutexTestThread* t = &threads[i];
    t->lock = &lock;
    t->nlocks = nlocks_per_thread;
    auto status = thrd_create(&t->t, hybridmutex_test_thread, t);
    asserteq(status, thrd_success);
  }

  for (u32 i = 0; i < countof(threads); i++) {
    auto t = &threads[i];
    int retval;
    thrd_join(t->t, &retval);
    asserteq(t->nlocks, 0);
  }

  HybridMutexDispose(&lock);
}


#endif /* R_TESTING_ENABLED */
ASSUME_NONNULL_END
