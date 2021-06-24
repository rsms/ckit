#include "rbase.h"
#include "chan.h"
//
// This implementation was inspired by the following projects, implementations and ideas:
// - golang.org/src/runtime/chan.go (main inspiration)
// - github.com/tempesta-tech/blog/blob/aaae716246041013b14ea0ae6ad287f84176b72b/
//   lockfree_rb_q.cc
// - github.com/tylertreat/chan/blob/ad341e95d785e7d38dc9de052fb18a3c35c74977/src/chan.c
// - github.com/oneapi-src/oneTBB/blob/fbc48b39c61ad1358a91c5058a8996b418518480/
//   include/oneapi/tbb/concurrent_queue.h
// - github.com/craflin/LockFreeQueue/blob/064c8d17b3032c5e9d30003798982a13508d4b49/
//   LockFreeQueueCpp11.h
// - github.com/cameron314/concurrentqueue/blob/87406493650f46ab59a534122e15cc68f4ba106b/
//   c_api/blockingconcurrentqueue.cpp
// - github.com/apple/swift-corelibs-libdispatch/blob/a181700dbf6aee3082a1c6074b3aab97560b5ef8/
//   src/queue.c
//

// DEBUG_CHAN_LOG: define to enable debug logging of send and recv
//#define DEBUG_CHAN_LOG

// DEBUG_CHAN_LOCK: define to enable debug logging of channel locks
//#define DEBUG_CHAN_LOCK


// LINE_CACHE_SIZE is the size of a cache line of the target CPU.
// The value 64 covers i386, x86_64, arm32, arm64.
// Note that Intel TBB uses 128 (max_nfs_size).
// TODO: set value depending on target preprocessor information.
#define LINE_CACHE_SIZE 64

//#define ATTR_ALIGNED_LINE_CACHE __attribute__((aligned(LINE_CACHE_SIZE)))


ASSUME_NONNULL_BEGIN

// ----------------------------------------------------------------------------
// debugging

#if defined(DEBUG_CHAN_LOG) && !defined(DEBUG)
  #undef DEBUG_CHAN_LOG
#endif
#ifdef DEBUG_CHAN_LOG
  #define THREAD_ID_INVALID  SIZE_MAX

  static size_t thread_id() {
    static thread_local size_t _thread_id = THREAD_ID_INVALID;
    static atomic_size _thread_id_counter = ATOMIC_VAR_INIT(0);
    size_t tid = _thread_id;
    if (tid == THREAD_ID_INVALID) {
      tid = AtomicAdd(&_thread_id_counter, 1);
      _thread_id = tid;
    }
    return tid;
  }

  static const char* tcolor() {
    static const char* colors[] = {
      //"\x1b[1m",  // bold (white)
      "\x1b[93m", // yellow
      "\x1b[92m", // green
      "\x1b[91m", // red
      "\x1b[94m", // blue
      "\x1b[96m", // cyan
      "\x1b[95m", // magenta
    };
    return colors[thread_id() % countof(colors)];
  }

  // _dlog_chan writes a log message to stderr along with a globally unique "causality"
  // sequence number. It does not use libc FILEs as those use mutex locks which would alter
  // the behavior of multi-threaded channel operations. Instead it uses a buffer on the stack,
  // which of course is local per thread and then calls the write syscall with the one buffer.
  // This all means that log messages may be out of order; use the "causality" sequence number
  // to understand what other messages were produced according to the CPU.
  ATTR_FORMAT(printf, 2, 3)
  static void _dlog_chan(const char* fname, const char* fmt, ...) {
    static atomic_u32 seqnext = ATOMIC_VAR_INIT(1); // start at 1 to map to line nubmers

    u32 seq = AtomicAddx(&seqnext, 1, memory_order_acquire);

    char buf[256];
    const ssize_t bufcap = (ssize_t)sizeof(buf);
    ssize_t buflen = 0;

    buflen += (ssize_t)snprintf(&buf[buflen], bufcap - buflen,
      "%04u \x1b[1m%sT%02zu ", seq, tcolor(), thread_id());

    va_list ap;
    va_start(ap, fmt);
    buflen += (ssize_t)vsnprintf(&buf[buflen], bufcap - buflen, fmt, ap);
    va_end(ap);

    if (buflen > 0) {
      buflen += (ssize_t)snprintf(&buf[buflen], bufcap - buflen, "\x1b[0m (%s)\n", fname);
      if (buflen < 0) {
        // truncated; make sure to end the line
        buf[buflen - 1] = '\n';
      }
    }

    #undef FMT
    write(STDERR_FILENO, buf, buflen);
  }

  #define dlog_chan(fmt, ...) _dlog_chan(__FUNCTION__, fmt, ##__VA_ARGS__)
  #define dlog_send(fmt, ...) dlog_chan("send: " fmt, ##__VA_ARGS__)
  #define dlog_recv(fmt, ...) dlog_chan("recv: " fmt, ##__VA_ARGS__)
  // #define dlog_send(fmt, ...) do{}while(0)
  // #define dlog_recv(fmt, ...) do{}while(0)
#else
  #define dlog_chan(fmt, ...) do{}while(0)
  #define dlog_send(fmt, ...) do{}while(0)
  #define dlog_recv(fmt, ...) do{}while(0)
#endif


// ----------------------------------------------------------------------------
// misc utils

#define is_power_of_two(intval) \
  (intval) && (0 == ((intval) & ((intval) - 1)))

// is_aligned checks if passed in pointer is aligned on a specific border.
// bool is_aligned<T>(T* pointer, uintptr_t alignment)
#define is_aligned(pointer, alignment) \
  (0 == ((uintptr_t)(pointer) & (((uintptr_t)alignment) - 1)))


// -------------------------------------------------------------------------
// channel lock

#ifdef DEBUG_CHAN_LOCK
  static u32 chlock_count = 0;

  #define CHAN_LOCK_T             mtx_t
  #define chan_lock_init(lock)    mtx_init((lock), mtx_plain)
  #define chan_lock_dispose(lock) mtx_destroy(lock)

  #define chan_lock(lock) do{                  \
    u32 n = chlock_count++;                    \
    dlog("CL #%u LOCK %s:%d", n, __FILE__, __LINE__); \
    mtx_lock(lock);                            \
    dlog("CL #%u UNLOCK %s:%d", n, __FILE__, __LINE__); \
  }while(0)

  #define chan_unlock(lock) do{                  \
    /*dlog("CL UNLOCK %s:%d", __FILE__, __LINE__);*/ \
    mtx_unlock(lock);                            \
  }while(0)
#else
  // // mtx_t
  // #define CHAN_LOCK_T             mtx_t
  // #define chan_lock_init(lock)    mtx_init((lock), mtx_plain)
  // #define chan_lock_dispose(lock) mtx_destroy(lock)
  // #define chan_lock(lock)         mtx_lock(lock)
  // #define chan_unlock(lock)       mtx_unlock(lock)

  // HybridMutex
  #define CHAN_LOCK_T             HybridMutex
  #define chan_lock_init(lock)    HybridMutexInit(lock)
  #define chan_lock_dispose(lock) HybridMutexDispose(lock)
  #define chan_lock(lock)         HybridMutexLock(lock)
  #define chan_unlock(lock)       HybridMutexUnlock(lock)
#endif

// -------------------------------------------------------------------------

typedef struct Thr Thr;
typedef uintptr_t Msg;

struct Thr {
  Thr*          next;    // list link
  size_t        id;
  LSema         sema;
  bool          init;
  atomic_bool   closed;
  _Atomic(Msg*) msgptr;
};

typedef struct WaitQ {
  _Atomic(Thr*) first; // head of linked list of parked threads
  _Atomic(Thr*) last;  // tail of linked list of parked threads
} WaitQ;

typedef struct Chan {
  uintptr_t   memptr; // memory allocation pointer
  Mem         mem;    // memory allocator this belongs to (immutable)
  u32         qcap;   // size of the circular queue buf (immutable)
  atomic_u32  qlen;   // number of messages currently queued in buf
  atomic_u32  closed; // 1 when closed
  CHAN_LOCK_T lock;

  WaitQ sendq; // list of waiting send callers
  WaitQ recvq; // list of waiting recv callers

  atomic_u32 sendx; // send index in buf
  atomic_u32 recvx; // receive index in buf

  Msg buf[]; // queue storage
} Chan;


static void thr_init(Thr* t) {
  static atomic_size _thread_id_counter = ATOMIC_VAR_INIT(0);

  t->id = AtomicAdd(&_thread_id_counter, 1);
  t->init = true;
  LSemaInit(&t->sema, 0); // TODO: SemaDispose?
}


inline static Thr* thr_current() {
  static thread_local Thr _thr = {0};

  Thr* t = &_thr;
  if (R_UNLIKELY(!t->init))
    thr_init(t);
  return t;
}


inline static void thr_signal(Thr* t) {
  LSemaSignal(&t->sema, 1); // wake
}


inline static void thr_wait(Thr* t) {
  dlog_chan("thr_wait ...");
  LSemaWait(&t->sema); // sleep
}


static void wq_enqueue(WaitQ* wq, Thr* t) {
  // note: atomic loads & stores for cache reasons, not thread safety; c->lock is held.
  if (AtomicLoadAcq(&wq->first)) {
    // Note: compare first instead of last as we don't clear wq->last in wq_dequeue
    AtomicLoadAcq(&wq->last)->next = t;
  } else {
    AtomicStoreRel(&wq->first, t);
  }
  AtomicStoreRel(&wq->last, t);
}


inline static Thr* nullable wq_dequeue(WaitQ* wq) {
  Thr* t = AtomicLoadAcq(&wq->first);
  if (t) {
    AtomicStoreRel(&wq->first, t->next);
    t->next = NULL;
    // Note: intentionally not clearing wq->last in case wq->first==wq->last as we can
    // avoid that branch by not checking wq->last in wq_enqueue.
  }
  return t;
}


// chan_park adds msgptr to wait queue wq, unlocks channel c and blocks the calling thread
static Thr* chan_park(Chan* c, WaitQ* wq, Msg* msgptr) {
  // caller must hold lock on channel that owns wq
  auto t = thr_current();
  AtomicStore(&t->msgptr, msgptr);
  dlog_chan("park: msgptr %p", msgptr);
  wq_enqueue(wq, t);
  chan_unlock(&c->lock);
  thr_wait(t);
  return t;
}


inline static bool chan_isfull(Chan* c) {
  // c.dataqsiz is immutable (never written after the channel is created)
  // so it is safe to read at any time during channel operation.
  if (c->qcap == 0)
    return AtomicLoad(&c->recvq.first) == NULL;
  return AtomicLoad(&c->qlen) == c->qcap;
}


static bool chan_send_direct(Chan* c, Msg msg, Thr* recvt) {
  // chan_send_direct processes a send operation on an empty channel c.
  // msg sent by the sender is copied to the receiver recvt.
  // The receiver is then woken up to go on its merry way.
  // Channel c must be empty and locked. This function unlocks c with chan_unlock.
  // recvt must already be dequeued from c.

  // *recvt->msgptr = msg
  // recvt->msgptr = NULL
  Msg* msgptr = AtomicLoadAcq(&recvt->msgptr);
  assertnotnull(msgptr);
  dlog_send("direct send of msg %zu to [%zu] (msgptr %p)", (size_t)msg, recvt->id, msgptr);
  *msgptr = msg; // store to address provided with chan_recv call
  AtomicStore(&recvt->msgptr, NULL);
  //atomic_thread_fence(memory_order_seq_cst);

  chan_unlock(&c->lock);
  thr_signal(recvt); // wake up chan_recv caller
  return true;
}


inline static bool chan_send(Chan* c, Msg msg, bool* nullable closed) {
  bool block = closed == NULL;
  dlog_send("msg %zu", (size_t)msg);

  // fast path for non-blocking send on full channel
  if (!block && AtomicLoad(&c->closed) == 0 && chan_isfull(c))
    return false;

  chan_lock(&c->lock);

  if (R_UNLIKELY(AtomicLoad(&c->closed))) {
    chan_unlock(&c->lock);
    if (block) {
      panic("send on closed channel");
    } else {
      *closed = true;
    }
    return false;
  }

  Thr* recvt = wq_dequeue(&c->recvq);
  if (recvt) {
    // Found a waiting receiver. recvt is blocked, waiting in chan_recv.
    // We pass the value we want to send directly to the receiver,
    // bypassing the channel buffer (if any).
    // Note that chan_send_direct calls chan_unlock(&c->lock).
    assert(recvt->init);
    return chan_send_direct(c, msg, recvt);
  }

  if (AtomicLoad(&c->qlen) < c->qcap) {
    // space available in message buffer -- enqueue
    u32 i = AtomicAdd(&c->sendx, 1);
    c->buf[i] = msg;
    dlog_send("enqueue msg %zu at buf[%u]", (size_t)msg, i);
    if (i == c->qcap - 1)
      AtomicStore(&c->sendx, 0);
    AtomicAdd(&c->qlen, 1);
    chan_unlock(&c->lock);
  } else {
    // park the calling thread. Some recv caller will wake us up.
    // Note that chan_park calls chan_unlock(&c->lock)
    dlog_send("wait... (msgptr %p)", &msg);
    chan_park(c, &c->sendq, &msg);
    dlog_send("woke up -- sent message %zu", (size_t)msg);
  }
  return true;
}


static bool chan_recv_direct(Chan* c, Msg* dstmsgptr, Thr* st);

inline static bool chan_recv(Chan* c, Msg* msgptr, bool* nullable closed) {
  bool block = closed == NULL; // TODO: non-blocking path
  dlog_recv("msgptr %p", msgptr);

  chan_lock(&c->lock);

  if (AtomicLoad(&c->closed) && AtomicLoad(&c->qlen) == 0) {
    // channel is closed and the buffer queue is empty
    dlog_recv("channel closed & empty queue");
    chan_unlock(&c->lock);
    *msgptr = 0; // avoid undefined behavior
    if (closed)
      *closed = true;
    return false;
  }

  Thr* t = wq_dequeue(&c->sendq);
  if (t) {
    // Found a waiting sender.
    // If buffer is size 0, receive value directly from sender.
    // Otherwise, receive from head of queue and add sender's value to the tail of the queue
    // (both map to the same buffer slot because the queue is full).
    // Note that chan_recv_direct calls chan_unlock(&c->lock).
    assert(t->init);
    return chan_recv_direct(c, msgptr, t);
  }

  if (AtomicLoad(&c->qlen) > 0) {
    // Receive directly from queue
    u32 i = AtomicAdd(&c->recvx, 1);
    if (i == c->qcap - 1)
      AtomicStore(&c->recvx, 0);
    AtomicSub(&c->qlen, 1);

    *msgptr = c->buf[i];
    #ifdef DEBUG
    c->buf[i] = 0;
    #endif

    dlog_recv("dequeue msg %zu from buf[%u]", (size_t)*msgptr, i);
    assert(*msgptr > 0);

    chan_unlock(&c->lock);
    return true;
  }

  // No message available -- empty queue and no waiting senders

  // Check if the channel is closed.
  if (AtomicLoad(&c->closed)) {
    chan_unlock(&c->lock);
    goto ret_closed;
  }

  // Block by parking the thread. Some send caller will wake us up.
  // Note that chan_park calls chan_unlock(&c->lock)
  dlog_recv("wait... (msgptr %p)", msgptr);
  t = chan_park(c, &c->recvq, msgptr);

  // woken up by sender or close call
  if (AtomicLoad(&t->closed)) {
    // Note that we check "closed" on the Thr, not the Chan.
    // This is important since c->closed may be true even as we receive a message.
    dlog_recv("woke up -- channel closed");
    goto ret_closed;
  }

  // message was delivered by storing to msgptr by some sender
  dlog_recv("woke up -- received message %zu", (size_t)*msgptr);
  return true;

ret_closed:
  dlog_recv("channel closed");
  *msgptr = 0; // avoid undefined behavior
  return false;
}


// chan_recv_direct processes a receive operation on a full channel c
static bool chan_recv_direct(Chan* c, Msg* dstmsgptr, Thr* sendert) {
  // There are 2 parts:
  // 1) The value sent by the sender sg is put into the channel and the sender
  //    is woken up to go on its merry way.
  // 2) The value received by the receiver (the current G) is written to ep.
  // For synchronous (unbuffered) channels, both values are the same.
  // For asynchronous (buffered) channels, the receiver gets its data from
  // the channel buffer and the sender's data is put in the channel buffer.
  // Channel c must be full and locked.
  // sendert must already be dequeued from c.sendq.
  bool ok = true;

  if (AtomicLoad(&c->qlen) == 0) {
    // Copy data from sender
    dlog_recv("direct recv of msg %zu from [%zu] (msgptr %p, buffer empty)",
      (size_t)*sendert->msgptr, sendert->id, sendert->msgptr);
    Msg* srcmsgptr = AtomicLoadx(&sendert->msgptr, memory_order_consume);
    assertnotnull(srcmsgptr);
    *dstmsgptr = *srcmsgptr;
  } else {
    // Queue is full. Take the item at the head of the queue.
    // Make the sender enqueue its item at the tail of the queue.
    // Since the queue is full, those are both the same slot.
    dlog_recv("direct recv of msg %zu from [%zu] (msgptr %p, buffer full)",
      (size_t)*sendert->msgptr, sendert->id, sendert->msgptr);
    //assert_debug(AtomicLoad(&c->qlen) == c->qcap); // queue is full

    // copy msg from queue to receiver
    u32 i = AtomicAdd(&c->recvx, 1);
    if (i == c->qcap - 1) {
      AtomicStore(&c->recvx, 0);
      AtomicStore(&c->sendx, 0);
    } else {
      AtomicStore(&c->sendx, i + 1);
    }

    *dstmsgptr = c->buf[i];
    dlog_recv("dequeue msg %zu from buf[%u]", (size_t)c->buf[i], i);
    assert(c->buf[i] > 0);

    // copy msg from sender to queue
    Msg* srcmsgptr = AtomicLoadx(&sendert->msgptr, memory_order_consume);
    assertnotnull(srcmsgptr);
    dlog_recv("enqueue msg %zu at buf[%u]", (size_t)*srcmsgptr, i);
    c->buf[i] = *srcmsgptr;
  }

  chan_unlock(&c->lock);
  thr_signal(sendert); // wake up chan_send caller
  return ok;
}


Chan* nullable ChanOpen(Mem mem, u32 cap) {
  i64 memsize = (i64)sizeof(Chan) + ((i64)cap * (i64)sizeof(Msg));

  // ensure we have enough space to offset the allocation by line cache (for alignment)
  memsize = align2(memsize + ((LINE_CACHE_SIZE+1) / 2), LINE_CACHE_SIZE);

  // check for overflow (since we use the cap argument in the mix)
  if (memsize < (i64)sizeof(Chan))
    panic("buffer size out of range");

  // allocate memory, placing Chan at a line cache address boundary
  uintptr_t ptr = (uintptr_t)memalloc(mem, memsize);

  // align c to line cache boundary
  Chan* c = (Chan*)align2(ptr, LINE_CACHE_SIZE);

  c->memptr = ptr;
  c->mem = mem;
  c->qcap = cap;
  chan_lock_init(&c->lock);

  // make sure that the thread setting up the channel gets a low thread_id
  #ifdef DEBUG_CHAN_LOG
  thread_id();
  #endif

  return c;
}


void ChanClose(Chan* c) {
  dlog_chan("--- close ---");

  chan_lock(&c->lock);
  dlog_chan("close: channel locked");

  if (atomic_exchange_explicit(&c->closed, 1, memory_order_acquire) != 0)
    panic("close of closed channel");
  atomic_thread_fence(memory_order_seq_cst);

  Thr* t = AtomicLoadAcq(&c->recvq.first);
  while (t) {
    dlog_chan("close: wake recv [%zu]", t->id);
    Thr* next = t->next;
    AtomicStore(&t->closed, true);
    thr_signal(t);
    t = next;
  }

  t = AtomicLoadAcq(&c->sendq.first);
  while (t) {
    dlog_chan("close: wake send [%zu]", t->id);
    Thr* next = t->next;
    AtomicStore(&t->closed, true);
    thr_signal(t);
    t = next;
  }

  chan_unlock(&c->lock);
  dlog_chan("close: done");
}


void ChanFree(Chan* c) {
  assert(AtomicLoadAcq(&c->closed)); // must close channel before freeing its memory
  chan_lock_dispose(&c->lock);
  memfree(c->mem, (void*)c->memptr);
}


u32  ChanCap(const Chan* c) { return c->qcap; }
bool ChanSend(Chan* c, Msg msg)                      { return chan_send(c, msg, NULL); }
bool ChanTrySend(Chan* c, bool* closed, Msg msg)     { return chan_send(c, msg, closed); }
bool ChanRecv(Chan* c, Msg* result)                  { return chan_recv(c, result, NULL); }
bool ChanTryRecv(Chan* c, bool* closed, Msg* result) { return chan_recv(c, result, closed); }


ASSUME_NONNULL_END