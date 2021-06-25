//
// Run these benchmarks with:
//   ckit build -fast chan_bench && ./out/fast/chan_bench
//
// Alternatively with incremental compilation:
//   ckit watch -fast -r chan_bench
//
// Usage: chan_bench [<milliseconds>]
// <milliseconds>  How long to sample each benchmark. Defaults to 1000.
//
#include "rbase.h"
#include "chan.h"
#include "bench_impl.h"

ASSUME_NONNULL_BEGIN

int main(int argc, const char** argv) {
  return benchmark_main(argc, argv);
}

// Msg is the test message type
typedef u32 Msg;


static u64 init_test_messages(Msg* messages, u32 nmessages) {
  // init messages (1 2 3 ...)
  u64 messages_sum = 0; // sum of all messages
  for (u32 i = 0; i < nmessages; i++) {
    Msg msg = (Msg)i + 1; // make it 1-based for simplicity
    messages[i] = msg;
    messages_sum += (u64)msg;
  }
  return messages_sum;
}

// ————————————————————————————————————————————————————————————————————————————————————————————

static const u32 st1_bufsize = 4;

static void st1_onbegin(Benchmark* b) {
  // bufsize must be >0 since this is a single-threaded test
  if (st1_bufsize == 0)
    panic("st1_bufsize must be >0");

  fprintf(stderr, "buffer size: %u\n", st1_bufsize);

  // We perform st1_bufsize x ChanSend + st1_bufsize x ChanRecv per N iteration,
  // so set the N_divisor so that the "avg time/op" stat makes more sense.
  b->N_divisor = (size_t)(st1_bufsize + st1_bufsize);
}

R_BENCHMARK(st1, st1_onbegin)(Benchmark* b) {
  // single-threaded, buffered, lock-step send, recv, send, recv, ...
  Mem mem = MemLibC();

  u32 messages_count = b->N;
  Msg* messages = memalloc(mem, sizeof(Msg) * (size_t)messages_count);

  #if !defined(NDEBUG)
  u64 recv_messages_sum = 0; // sum of all messages received
  u64 send_messages_sum =
  #endif
    init_test_messages(messages, messages_count);

  Chan* ch = ChanOpen(mem, sizeof(Msg), st1_bufsize);

  auto timer = TimerStart();

  for (u32 i = 0; i < messages_count; i += st1_bufsize) {
    for (u32 j = 0; j < st1_bufsize; j++) {
      ChanSend(ch, &messages[i + j]);
    }
    for (u32 j = 0; j < st1_bufsize; j++) {
      Msg msg_out;
      UNUSED bool ok = ChanRecv(ch, &msg_out);

      #if !defined(NDEBUG)
      assert(ok);
      assert(messages[i + j] == msg_out);
      recv_messages_sum += (u64)msg_out;
      #endif
    }
  }

  TimerStop(&timer);

  #if !defined(NDEBUG)
  assert(send_messages_sum == recv_messages_sum);
  #endif

  ChanClose(ch);
  ChanFree(ch);
  memfree(mem, messages);

  return timer;
}

// ————————————————————————————————————————————————————————————————————————————————————————————
// threads


typedef struct TestThread {
  thrd_t t;
  u32    id;
  Chan*  ch;
  Timer  timer;

  // messages to be sent (fields used by send_thread)
  u32  send_msglen; // messages in msgv
  Msg* send_msgv;   // messages

  // received messages (fields used by recv_thread)
  u32 recv_msgc; // number of messages received
  u64 recv_sum;  // sum of messages received
} TestThread;


static int send_thread(void* tptr) {
  auto t = (TestThread*)tptr;
  auto msgv = t->send_msgv;
  t->timer = TimerStart();
  for (u32 i = 0; i < t->send_msglen; i++) {
    TimerCycleSampleStart(&t->timer);
    UNUSED bool ok = ChanSend(t->ch, &msgv[i]);
    TimerCycleSampleStop(&t->timer);
    assert(ok);
  }
  TimerStop(&t->timer);
  //dlog("[send_thread#%u] exit", t->id);
  return 0;
}


static int recv_thread(void* tptr) {
  auto t = (TestThread*)tptr;
  //fprintf(stderr, "thread %u start\n", t->id);
  Chan* ch = t->ch;
  u32 recv_msgc = 0;
  Msg recv_sum = 0;
  t->timer = TimerStart();
  while (1) {
    // receive a message
    Msg msg;
    if (!ChanRecv(ch, &msg)) {
      break; // channel closed
    }
    recv_msgc++;
    recv_sum += msg;
  }
  TimerStop(&t->timer);
  t->recv_msgc = recv_msgc;
  t->recv_sum = (u64)recv_sum;
  //fprintf(stderr, "thread %u exit\n", t->id);
  return 0;
}


struct {
  u32 bufsize;
  u32 n_send_threads;
  u32 n_recv_threads;
} mt1_conf = {0};

static void onbegin(Benchmark* b) {
  fprintf(stderr, "using %u sender threads, %u receiver threads (bufsize %u)\n",
    mt1_conf.n_send_threads, mt1_conf.n_recv_threads, mt1_conf.bufsize);
}

static Timer mt1_sampler(Benchmark* b);

#define DEF_MT1_BENCHMARK(name, init) \
  static void name##_onbegin(Benchmark* b) { \
    UNUSED auto ncpu = os_ncpu();                   \
    init                                     \
    onbegin(b);                              \
  }                                          \
  R_BENCHMARK(name, name##_onbegin)(Benchmark* b) { return mt1_sampler(b); }


DEF_MT1_BENCHMARK(mt1_1_1_buffered, {
  mt1_conf.bufsize = 4;
  mt1_conf.n_send_threads = 1;
  mt1_conf.n_recv_threads = 1;
})

DEF_MT1_BENCHMARK(mt1_1_N_buffered, {
  mt1_conf.bufsize = MAX(1, ncpu / 2);
  mt1_conf.n_send_threads = 1;
  mt1_conf.n_recv_threads = ncpu;
})

DEF_MT1_BENCHMARK(mt1_N_N_buffered, {
  mt1_conf.bufsize = MAX(1, ncpu / 2);
  mt1_conf.n_send_threads = ncpu;
  mt1_conf.n_recv_threads = ncpu;
})

DEF_MT1_BENCHMARK(mt1_N_N_unbuffered, {
  mt1_conf.bufsize = 0;
  mt1_conf.n_send_threads = ncpu;
  mt1_conf.n_recv_threads = ncpu;
})

DEF_MT1_BENCHMARK(mt1_N2_N2_buffered, {
  mt1_conf.bufsize = MAX(1, ncpu / 4);
  mt1_conf.n_send_threads = MAX(1, ncpu / 2);
  mt1_conf.n_recv_threads = MAX(1, ncpu / 2);
})


static Timer mt1_sampler(Benchmark* b) {
  u32 messages_count = b->N;
  auto conf = mt1_conf;

  Mem mem = MemLibC();

  TestThread* send_threads = memalloc(mem, conf.n_send_threads * sizeof(TestThread));
  TestThread* recv_threads = memalloc(mem, conf.n_recv_threads * sizeof(TestThread));
  Msg* messages = memalloc(mem, messages_count * sizeof(Msg));

  Chan* ch = ChanOpen(mem, sizeof(Msg), conf.bufsize);

  // init messages (1 2 3 ...)
  u64 send_sum = 0; // sum of all messages
  for (u32 i = 0; i < messages_count; i++) {
    Msg msg = (Msg)i + 1; // make it 1-based for simplicity
    messages[i] = msg;
    send_sum += (u64)msg;
  }

  // init & spawn sender threads
  u32 send_messages_i = 0;
  const u32 send_messages_n = messages_count / conf.n_send_threads;
  for (u32 i = 0; i < conf.n_send_threads; i++) {
    TestThread* t = &send_threads[i];
    t->id = i + 1;
    t->ch = ch;

    assert(send_messages_i < messages_count);
    t->send_msgv = &messages[send_messages_i];

    u32 end_i = (
      i < conf.n_send_threads-1 ? MIN(send_messages_i + send_messages_n, messages_count) :
      messages_count // last chunk
    );
    // dlog("send_thread %u sends messages [%02u-%02u)", t->id, send_messages_i, end_i);
    t->send_msglen = end_i - send_messages_i;
    send_messages_i = end_i;

    UNUSED auto status = thrd_create(&t->t, send_thread, t);
    asserteq(status, thrd_success);
  }

  // init & spawn receiver threads
  for (u32 i = 0; i < conf.n_recv_threads; i++) {
    TestThread* t = &recv_threads[i];
    t->id = i + 1;
    t->ch = ch;
    UNUSED auto status = thrd_create(&t->t, recv_thread, t);
    asserteq(status, thrd_success);
  }

  // wait for all messages to be sent
  //dlog("waiting for %u messages to be sent by %u threads...",
  //  messages_count, conf.n_send_threads);
  for (u32 i = 0; i < conf.n_send_threads; i++) {
    int retval;
    thrd_join(send_threads[i].t, &retval);
  }
  //dlog("done sending %u messages", messages_count);
  //msleep(100);

  // close the channel and wait for recv_threads to exit
  ChanClose(ch);
  for (u32 i = 0; i < conf.n_recv_threads; i++) {
    auto t = &recv_threads[i];
    int retval;
    thrd_join(t->t, &retval);
  }
  ChanFree(ch);

  // check results and sum time
  atomic_thread_fence(memory_order_seq_cst);

  u64 send_time_sum = 0; // total time taken by all send_threads
  u64 send_utime_sum = 0;
  for (u32 i = 0; i < conf.n_send_threads; i++) {
    auto t = &send_threads[i];
    send_time_sum  += t->timer.time;
    send_utime_sum += t->timer.utime;
  }

  u32 recv_count = 0;    // tally of total number of messages all recv_threads received
  u64 recv_sum = 0;      // sum of all messages received by all recv_threads
  u64 recv_time_sum = 0; // total time taken by all recv_threads
  u64 recv_utime_sum = 0;
  for (u32 i = 0; i < conf.n_recv_threads; i++) {
    auto t = &recv_threads[i];
    recv_time_sum += t->timer.time;
    recv_utime_sum += t->timer.utime;
    recv_sum += t->recv_sum;
    recv_count += t->recv_msgc;
  }

  // light correctness checks (chan_test does more comprehensive testing)
  if (recv_sum != send_sum)
    panic("recv_sum != send_sum (" FMT_U64 " != " FMT_U64 ")", recv_sum, send_sum);
  if (recv_count != messages_count)
    panic("recv_count != messages_count (%u != %u)", recv_count, messages_count);

  // add the average of time used by threads
  Timer timer = {
    .time = ((send_time_sum / conf.n_send_threads)
          + (recv_time_sum / conf.n_recv_threads))
          / 2,
    .utime = ((send_utime_sum / conf.n_send_threads)
          + (recv_utime_sum / conf.n_recv_threads))
          / 2,
  };

  memfree(mem, messages);
  memfree(mem, send_threads);
  memfree(mem, recv_threads);
  return timer;
}



ASSUME_NONNULL_END
