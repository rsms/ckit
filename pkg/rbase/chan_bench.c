#include "rbase.h"
#include "chan.h"
#include "bench_impl.h"

ASSUME_NONNULL_BEGIN

int main(int argc, const char** argv) {
  return benchmark_main(argc, argv);
}

// Msg is the test message type
typedef uintptr_t Msg;


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

  Chan* ch = ChanOpen(mem, st1_bufsize);

  auto timer = TimerStart();

  for (u32 i = 0; i < messages_count; i += st1_bufsize) {
    for (u32 j = 0; j < st1_bufsize; j++) {
      ChanSend(ch, messages[i + j]);
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
  u32    recv_msgc; // number of messages received
  u64    recv_sum;  // sum of messages received
  Timer  timer;
} TestThread;


static int recv_thread(void* tptr) {
  auto t = (TestThread*)tptr;
  //fprintf(stderr, "thread %u start\n", t->id);
  t->timer = TimerStart();
  Chan* ch = t->ch;
  u32 recv_msgc = 0;
  Msg recv_sum = 0;
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


static u32 mt1_bufsize = 1;


static void mt1_onbegin(Benchmark* b) {
  // u32 thread_count = os_ncpu();
  u32 thread_count = 8;
  mt1_bufsize = thread_count / 2;
  b->userdata = (uintptr_t)thread_count;
  fprintf(stderr, "using 1 sender thread, %u receiver threads. Buffer size: %u\n",
    thread_count, mt1_bufsize);
}


R_BENCHMARK(mt1, mt1_onbegin)(Benchmark* b) {
  u32 messages_count = b->N;
  u32 thread_count = (u32)b->userdata;

  Mem mem = MemLibC();

  TestThread* threads = memalloc(mem, thread_count * sizeof(TestThread));
  Msg* messages = memalloc(mem, messages_count * sizeof(Msg));

  Chan* ch = ChanOpen(mem, /*bufsize*/mt1_bufsize);

  // init messages (1 2 3 ...)
  u64 send_message_sum = 0; // sum of all messages
  for (u32 i = 0; i < messages_count; i++) {
    Msg msg = (Msg)i + 1; // make it 1-based for simplicity
    messages[i] = msg;
    send_message_sum += (u64)msg;
  }

  // init & spawn threads
  dlog("spawning %u receiver threads", thread_count);
  for (u32 i = 0; i < thread_count; i++) {
    TestThread* t = &threads[i];
    t->id = i + 1;
    t->ch = ch;
    UNUSED auto status = thrd_create(&t->t, recv_thread, t);
    asserteq(status, thrd_success);
  }

  // send messages
  dlog("sending %u messages (values 1 2 3 ...)", messages_count);
  auto timer = TimerStart();
  for (u32 i = 0; i < messages_count; i++) {
    Msg msg = messages[i];
    ChanSend(ch, msg);
  }
  TimerStop(&timer);

  //msleep(100); // XXX FIXME

  // close the channel and wait for threads to exit
  ChanClose(ch);
  for (u32 i = 0; i < thread_count; i++) {
    auto t = &threads[i];
    int retval;
    thrd_join(t->t, &retval);
  }
  ChanFree(ch);

  atomic_thread_fence(memory_order_seq_cst);

  // check results
  u32 recv_message_count = 0; // tally of total number of messages all threads received
  u64 recv_message_sum = 0;   // sum of all messages received by all threads
  u64 recv_time_sum = 0;      // total time taken by all threads
  for (u32 i = 0; i < thread_count; i++) {
    auto t = &threads[i];
    recv_time_sum += t->timer.time;
    recv_message_sum += t->recv_sum;
    recv_message_count += t->recv_msgc;
  }

  if (recv_message_sum != send_message_sum) {
    panic("recv_message_sum != send_message_sum (" FMT_U64 " != " FMT_U64 ")",
      recv_message_sum, send_message_sum);
  }

  if (recv_message_count != messages_count) {
    panic("recv_message_count != messages_count (%u != %u)",
      recv_message_count, messages_count);
  }

  // add the average of time used by threads to receive messages
  timer.time += recv_time_sum / thread_count;

  memfree(mem, messages);
  memfree(mem, threads);
  return timer;
}



ASSUME_NONNULL_END
