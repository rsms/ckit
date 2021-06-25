#include "rbase.h"
#include "chan.h"

ASSUME_NONNULL_BEGIN
#if R_TESTING_ENABLED

// DEBUG_LOG: define to enable dlog calls in tests
//#define DEBUG_LOG
#if !defined(DEBUG_LOG)
  #undef dlog
  #define dlog(...) ((void)0)
#endif


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


R_TEST(chan_st) {
  Mem mem = MemLibC();
  Msg messages[10]; // must be an even number
  u64 send_messages_sum = init_test_messages(messages, countof(messages));
  u64 recv_messages_sum = 0; // sum of all messages received

  size_t N = 2;
  Chan* ch = ChanOpen(mem, sizeof(Msg), /*bufsize*/N);

  for (size_t i = 0; i < countof(messages); i += N) {
    for (size_t j = 0; j < N; j++) {
      ChanSend(ch, &messages[i + j]);
    }
    for (size_t j = 0; j < N; j++) {
      Msg msg_out;
      assert(ChanRecv(ch, &msg_out));
      asserteq(messages[i + j], msg_out);
      recv_messages_sum += (u64)msg_out;
    }
  }

  asserteq(send_messages_sum, recv_messages_sum);

  ChanClose(ch);
  ChanFree(ch);
}


// TODO: test non-blocking ChanTrySend and ChanTryRecv


typedef struct TestThread {
  thrd_t t;
  u32    id;
  Chan*  ch;
  LSema* sema; // semaphore for signalling completion

  // messages received or to be sent
  u32  msgcap; // capacity of msgv
  u32  msglen; // messages in msgv
  Msg* msgv;   // messages
} TestThread;


static int send_thread(void* tptr) {
  auto t = (TestThread*)tptr;
  for (u32 i = 0; i < t->msglen; i++) {
    Msg msg = t->msgv[i];
    bool ok = ChanSend(t->ch, &msg);
    assertf(ok, "[send_thread#%u] channel closed during send", t->id);
  }
  //dlog("[send_thread#%u] exit", t->id);
  return 0;
}


static int recv_thread(void* tptr) {
  auto t = (TestThread*)tptr;
  t->msglen = 0;
  while (1) {
    // receive a message
    Msg msg;
    if (!ChanRecv(t->ch, &msg)) {
      break; // channel closed
    }

    // save received message for later inspection by test
    assertf(t->msglen < t->msgcap,
      "[recv_thread#%u] received an excessive number of messages (>%u)",
      t->id, t->msgcap);
    t->msgv[t->msglen++] = msg;

    // add some thread scheduling jitter
    // usleep(rand() % 10);
  }
  //LSemaSignal(t->sema, 1);
  //dlog("[recv_thread#%u] exit", t->id);
  return 0;
}


static void chan_1send_Nrecv(u32 bufcap, u32 n_send_threads, u32 n_recv_threads, u32 nmessages);

R_TEST(chan_1send_1recv_buffered)   { chan_1send_Nrecv(2,             1,             1, 80); }
R_TEST(chan_1send_Nrecv_buffered)   { chan_1send_Nrecv(2,             1, os_ncpu() + 1, 80); }
R_TEST(chan_Nsend_1recv_buffered)   { chan_1send_Nrecv(2, os_ncpu() + 1,             1, 80); }
R_TEST(chan_Nsend_Nrecv_buffered)   { chan_1send_Nrecv(2, os_ncpu() + 1, os_ncpu() + 1, 80); }
R_TEST(chan_1send_1recv_unbuffered) { chan_1send_Nrecv(0,             1,             1, 80); }
R_TEST(chan_1send_Nrecv_unbuffered) { chan_1send_Nrecv(0,             1, os_ncpu() + 1, 80); }
R_TEST(chan_Nsend_1recv_unbuffered) { chan_1send_Nrecv(0, os_ncpu() + 1,             1, 80); }
R_TEST(chan_Nsend_Nrecv_unbuffered) { chan_1send_Nrecv(0, os_ncpu() + 1, os_ncpu() + 1, 80); }

// R_TEST(chan_1send_Nrecv_buffered1) { chan_1send_Nrecv(1, 2, 2, 8); }


static void chan_1send_Nrecv(u32 bufcap, u32 n_send_threads, u32 n_recv_threads, u32 nmessages) {
  // serial sender, multiple receivers
  Mem mem = MemLibC();

  u32 send_message_count = MAX(n_recv_threads, n_send_threads) * nmessages;
  TestThread* recv_threads = memalloc(mem, sizeof(TestThread) * n_recv_threads);
  TestThread* send_threads = memalloc(mem, sizeof(TestThread) * n_send_threads);

  // allocate storage for messages
  // the calling "sender" thread uses send_message_count messages while each
  // receiver thread is given send_message_count message slots for reception
  // as in theory one thread may receive all messages.
  size_t message_storage_count = send_message_count * (n_recv_threads + 1);
  Msg* message_storage = memalloc(mem, message_storage_count * sizeof(Msg));
  Msg* send_messages = &message_storage[0];

  LSema sema;
  LSemaInit(&sema, 0);

  Chan* ch = ChanOpen(mem, sizeof(Msg), /*cap*/bufcap);
  dlog("channel capacity: %u, send_message_count: %zu", ChanCap(ch), send_message_count);

  // init messages (1 2 3 ...)
  u64 send_message_sum = 0; // sum of all messages
  for (u32 i = 0; i < send_message_count; i++) {
    Msg msg = (Msg)i + 1; // make it 1-based for simplicity
    send_messages[i] = msg;
    send_message_sum += (u64)msg;
  }

  dlog("spawning %u sender threads", n_send_threads);
  u32 send_messages_i = 0;
  const u32 send_messages_n = send_message_count / n_send_threads;
  for (u32 i = 0; i < n_send_threads; i++) {
    TestThread* t = &send_threads[i];
    t->id = i + 1;
    t->ch = ch;
    t->sema = &sema;
    t->msgcap = send_message_count;

    assert(send_messages_i < send_message_count);
    t->msgv = &send_messages[send_messages_i];

    u32 end_i = (
      i < n_send_threads-1 ? MIN(send_messages_i + send_messages_n, send_message_count) :
      send_message_count // last chunk
    );
    // dlog("send_thread %u sends messages [%02u-%02u)", t->id, send_messages_i, end_i);
    t->msglen = end_i - send_messages_i;
    send_messages_i = end_i;

    auto status = thrd_create(&t->t, send_thread, t);
    asserteq(status, thrd_success);
  }

  dlog("spawning %u receiver threads", n_recv_threads);
  for (u32 i = 0; i < n_recv_threads; i++) {
    TestThread* t = &recv_threads[i];
    t->id = i + 1;
    t->ch = ch;
    t->msgcap = send_message_count;
    t->msgv = &message_storage[(i + 1) * send_message_count];
    t->sema = &sema;
    auto status = thrd_create(&t->t, recv_thread, t);
    asserteq(status, thrd_success);
  }

  // wait for all messages to be sent
  dlog("waiting for %u messages to be sent by %u threads...", send_message_count, n_send_threads);
  for (u32 i = 0; i < n_send_threads; i++) {
    int retval;
    thrd_join(send_threads[i].t, &retval);
  }
  dlog("done sending %u messages", send_message_count);
  //msleep(100);

  // close the channel and wait for receiver threads to exit
  ChanClose(ch);
  dlog("waiting for %u receiver threads to finish...", n_recv_threads);
  for (u32 i = 0; i < n_recv_threads; i++) {
    int retval;
    thrd_join(recv_threads[i].t, &retval);
  }
  ChanFree(ch);

  // check results
  u32 recv_message_count = 0; // tally of total number of messages all threads received
  u64 recv_message_sum   = 0; // sum of all messages received
  for (u32 i = 0; i < n_recv_threads; i++) {
    auto t = &recv_threads[i];
    recv_message_count += t->msglen;
    for (u32 y = 0; y < t->msglen; y++) {
      recv_message_sum += (u64)t->msgv[y];
    }
  }
  asserteq(recv_message_count, send_message_count);
  asserteq(recv_message_sum, send_message_sum);

  memfree(mem, recv_threads);
  memfree(mem, send_threads);
  memfree(mem, send_messages);
}


#endif /*R_TESTING_ENABLED*/
ASSUME_NONNULL_END
