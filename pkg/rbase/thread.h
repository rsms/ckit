#pragma once

// YIELD_THREAD() yields for other threads to be scheduled on the current CPU by the OS
#if (defined(WIN32) || defined(_WIN32))
  #include <windows.h>
  #define YIELD_THREAD() ((void)0)
#else
  #include <sched.h>
  #define YIELD_THREAD() sched_yield()
#endif


// YIELD_CPU() yields for other work on a CPU core
#if defined(__i386) || defined(__i386__) || defined(__x86_64__)
  #define YIELD_CPU() __asm__ __volatile__("pause")
#elif defined(__arm__) || defined(__arm64__) || defined(__aarch64__)
  #define YIELD_CPU() __asm__ __volatile__("yield")
#elif defined(mips) || defined(__mips__) || defined(MIPS) || defined(_MIPS_) || defined(__mips64)
  #if defined(_ABI64) && (_MIPS_SIM == _ABI64)
    #define YIELD_CPU() __asm__ __volatile__("pause")
  #else
    // comment from WebKit source:
    //   The MIPS32 docs state that the PAUSE instruction is a no-op on older
    //   architectures (first added in MIPS32r2). To avoid assembler errors when
    //   targeting pre-r2, we must encode the instruction manually.
    #define YIELD_CPU() __asm__ __volatile__(".word 0x00000140")
  #endif
#elif (defined(WIN32) || defined(_WIN32))
  #include <immintrin.h>
  #define YIELD_CPU() _mm_pause()
#else
  // GCC & clang intrinsic
  #define YIELD_CPU() __builtin_ia32_pause()
#endif


// Portable C11 threads
#if defined(__STDC_NO_THREADS__) && __STDC_NO_THREADS__
  #include <pthread.h>

  #define ONCE_FLAG_INIT  PTHREAD_ONCE_INIT

  typedef pthread_t       thrd_t;
  typedef pthread_mutex_t mtx_t;
  typedef pthread_cond_t  cnd_t;
  typedef pthread_key_t   tss_t;
  typedef pthread_once_t  once_flag;

  typedef int  (*thrd_start_t)(void*);
  typedef void (*tss_dtor_t)(void*);

  enum { // bitflags
    mtx_plain     = 0,
    mtx_recursive = 1,
    mtx_timed     = 2,
  };

  enum {
    thrd_success,
    thrd_timedout,
    thrd_busy,
    thrd_error,
    thrd_nomem
  };

  #include "thread_pthread.h"

  #define THRD_NULLABLE nullable
#else
  #include <threads.h>
  #define THRD_NULLABLE
#endif

// function overview:
//
// int    thrd_create(thrd_t *thr, thrd_start_t func, void *arg);
// void   thrd_exit(int res);
// int    thrd_join(thrd_t thr, int *res);
// int    thrd_detach(thrd_t thr);
// thrd_t thrd_current(void);
// int    thrd_equal(thrd_t a, thrd_t b);
// int    thrd_sleep(const struct timespec *ts_in, struct timespec *rem_out);
// void   thrd_yield(void);
//
// int    mtx_init(mtx_t *mtx, int type);
// void   mtx_destroy(mtx_t *mtx);
// int    mtx_lock(mtx_t *mtx);
// int    mtx_trylock(mtx_t *mtx);
// int    mtx_timedlock(mtx_t *mtx, const struct timespec *ts);
// int    mtx_unlock(mtx_t *mtx);
//
// int    cnd_init(cnd_t *cond);
// void   cnd_destroy(cnd_t *cond);
// int    cnd_signal(cnd_t *cond);
// int    cnd_broadcast(cnd_t *cond);
// int    cnd_wait(cnd_t *cond, mtx_t *mtx);
// int    cnd_timedwait(cnd_t *cond, mtx_t *mtx, const struct timespec *ts);
//
// int    tss_create(tss_t *key, tss_dtor_t dtor);
// void   tss_delete(tss_t key);
// int    tss_set(tss_t key, void *val);
// void*  tss_get(tss_t key);
//
// void   call_once(once_flag *flag, void (*func)(void));
//

ASSUME_NONNULL_BEGIN

// Generic mutext functions
#define mutex_lock(m) _Generic((m), \
  mtx_t*:       mtx_lock, \
  rwmtx_t*:     rwmtx_lock, \
  SpinMutex*:   SpinMutexLock, \
  HybridMutex*: HybridMutexLock \
)(m)
#define mutex_unlock(m) _Generic((m), \
  mtx_t*:       mtx_unlock, \
  rwmtx_t*:     rwmtx_unlock, \
  SpinMutex*:   SpinMutexUnlock, \
  HybridMutex*: HybridMutexUnlock \
)(m)

// rwmtx_t is a read-write mutex.
// There can be many concurrent readers but only one writer.
// While no write lock is held, up to 16777214 read locks may be held.
// While a write lock is held no read locks or other write locks can be held.
typedef struct rwmtx_t {
  mtx_t      w; // writer lock
  atomic_u32 r; // reader count
} rwmtx_t;
static int  rwmtx_init(rwmtx_t* m, int wtype);
static void rwmtx_destroy(rwmtx_t* m);
int rwmtx_rlock(rwmtx_t* m);     // acquire read-only lock (blocks until acquired)
int rwmtx_tryrlock(rwmtx_t* m);  // attempt to acquire read-only lock (non-blocking)
int rwmtx_runlock(rwmtx_t* m);   // release read-only lock
int rwmtx_lock(rwmtx_t* m);      // acquire read+write lock (blocks until acquired)
int rwmtx_trylock(rwmtx_t* m);   // attempt to acquire read+write lock (non-blocking)
int rwmtx_unlock(rwmtx_t* m);    // release read+write lock


// Sema is a portable semaphore; a thin layer over the OS's semaphore implementation.
#if defined(_WIN32) || defined(__MACH__)
  typedef uintptr_t Sema; // intptr instead of void* to improve compiler diagnostics
#elif defined(__unix__)
  ASSUME_NONNULL_END
  #include <semaphore.h>
  ASSUME_NONNULL_BEGIN
  typedef sem_t Sema;
#endif /* Sema */

bool SemaInit(Sema*, u32 initcount); // returns false if system impl failed (rare)
void SemaDispose(Sema*);
bool SemaWait(Sema*);    // wait for a signal
bool SemaTryWait(Sema*); // try acquire a signal; return false instead of blocking
bool SemaTimedWait(Sema*, u64 timeout_usecs);
bool SemaSignal(Sema*, u32 count /*must be >0*/);


// LSema is a "light-weight" semaphore which is more efficient than Sema under
// high-contention condition, by avoiding syscalls.
// Waiting when there's already a signal available is extremely cheap and involves
// no syscalls. If there's no signal the implementation will retry by spinning for
// a short while before eventually falling back to Sema.
typedef struct LSema {
  atomic_ssize count;
  Sema         sema;
} LSema;

bool LSemaInit(LSema*, u32 initcount); // returns false if system impl failed (rare)
void LSemaDispose(LSema*);
bool LSemaWait(LSema*);
bool LSemaTryWait(LSema*);
bool LSemaTimedWait(LSema*, u64 timeout_usecs);
void LSemaSignal(LSema*, u32 count /*must be >0*/);
size_t LSemaApproxAvail(LSema*);


// SpinMutex is a mutex that spins rather than blocks when waiting for a lock
typedef struct SpinMutex {
  atomic_bool flag;
} SpinMutex;
static void SpinMutexInit(SpinMutex* m);
static void SpinMutexLock(SpinMutex* m);
static void SpinMutexUnlock(SpinMutex* m);


// HybridMutex is a mutex that will spin for a short while and then block
typedef struct HybridMutex {
  atomic_bool flag;
  atomic_i32  nwait;
  Sema        sema;
} HybridMutex;
static bool HybridMutexInit(HybridMutex* m); // returns false if system failed to init semaphore
static void HybridMutexDispose(HybridMutex* m);
static void HybridMutexLock(HybridMutex* m);
static void HybridMutexUnlock(HybridMutex* m);


// r_sync_once(flag, statement) -- execute code exactly once.
// Threads losing the race will wait for the winning thread to complete.
// Example use:
//   static r_sync_once_flag once;
//   r_sync_once(&once, { /* expensive work */ });
//
#define r_sync_once(flagptr, stmt) \
  if (R_UNLIKELY(AtomicLoadAcq(&(flagptr)->flag) < 3 && _sync_once_start(flagptr))) { \
    stmt; \
    _sync_once_end(flagptr); \
  }
typedef struct r_sync_once_flag {
  atomic_u32     flag;
  volatile mtx_t mu;
} r_sync_once_flag;
bool _sync_once_start(r_sync_once_flag* fl);
void _sync_once_end(r_sync_once_flag* fl);


// ----------------------------------------------------------------------------
//  inline implementations

static inline int rwmtx_init(rwmtx_t* m, int wtype) {
  assert(wtype != mtx_timed /* not supported */);
  m->r = ATOMIC_VAR_INIT(0);
  return mtx_init(&m->w, wtype);
}
static inline void rwmtx_destroy(rwmtx_t* m) { mtx_destroy(&m->w); }


// -----------------------
// SpinMutex

inline static void SpinMutexInit(SpinMutex* m) {
  m->flag = false;
}

void _spinMutexWait(SpinMutex* m);

inline static void SpinMutexLock(SpinMutex* m) {
  if (R_LIKELY(!atomic_exchange_explicit(&m->flag, true, r_memory_order(acquire))))
    return;
  _spinMutexWait(m);
}

inline static void SpinMutexUnlock(SpinMutex* m) {
  atomic_store_explicit(&m->flag, false, r_memory_order(release));
}

// -----------------------
// HybridMutex

inline static bool HybridMutexInit(HybridMutex* m) {
  m->flag = false;
  m->nwait = 0;
  return SemaInit(&m->sema, 0);
}

inline static void HybridMutexDispose(HybridMutex* m) {
  SemaDispose(&m->sema);
}

void _hybridMutexWait(HybridMutex* m);

inline static void HybridMutexLock(HybridMutex* m) {
  if (atomic_exchange_explicit(&m->flag, true, r_memory_order(acquire))) {
    // already locked -- slow path
    _hybridMutexWait(m);
  }
}

inline static void HybridMutexUnlock(HybridMutex* m) {
  atomic_exchange(&m->flag, false);
  if (AtomicLoad(&m->nwait) != 0) {
    // at least one thread waiting on a semaphore signal -- wake one thread
    SemaSignal(&m->sema, 1); // TODO: should we check the return value?
  }
}



ASSUME_NONNULL_END
