#include "rbase.h"
#include "chan.h"

ASSUME_NONNULL_BEGIN
#if R_TESTING_ENABLED


static u64 init_test_messages(uintptr_t* messages, u32 nmessages) {
  // init messages (1 2 3 ...)
  u64 messages_sum = 0; // sum of all messages
  for (u32 i = 0; i < nmessages; i++) {
    uintptr_t msg = (uintptr_t)i + 1; // make it 1-based for simplicity
    messages[i] = msg;
    messages_sum += (u64)msg;
  }
  return messages_sum;
}


R_TEST(chan_st) {
  Mem mem = MemLibC();
  uintptr_t messages[10]; // must be an even number
  u64 send_messages_sum = init_test_messages(messages, countof(messages));
  u64 recv_messages_sum = 0; // sum of all messages received

  size_t N = 2;
  Chan* ch = ChanOpen(mem, /*bufsize*/N);

  for (size_t i = 0; i < countof(messages); i += N) {
    for (size_t j = 0; j < N; j++) {
      ChanSend(ch, messages[i + j]);
    }
    for (size_t j = 0; j < N; j++) {
      uintptr_t msg_out;
      assert(ChanRecv(ch, &msg_out));
      asserteq(messages[i + j], msg_out);
      recv_messages_sum += (u64)msg_out;
    }
  }

  asserteq(send_messages_sum, recv_messages_sum);

  ChanClose(ch);
  ChanFree(ch);
}


// #define TEST_THREAD_COUNT  8
// #define TEST_MESSAGE_COUNT 2 // per thread
// #define TEST_TOTAL_MESSAGE_COUNT (TEST_MESSAGE_COUNT * TEST_THREAD_COUNT)


typedef struct TestThread {
  thrd_t t;
  u32    id;
  Chan*  ch;
  LSema* sema; // semaphore for signalling completion

  // messages received or to be sent
  u32        msgcap; // capacity of msgv
  u32        msglen; // messages in msgv
  uintptr_t* msgv;   // messages
} TestThread;


static int send_thread(void* tptr) {
  auto t = (TestThread*)tptr;
  for (u32 i = 0; i < t->msglen; i++) {
    uintptr_t msg = t->msgv[i];
    bool ok = ChanSend(t->ch, msg);
    assertf(ok, "[send_thread#%u] channel closed during send", t->id);
  }
  //dlog("[send_thread#%u] exit", t->id);
  return 0;
}


static int recv_thread(void* tptr) {
  auto t = (TestThread*)tptr;
  t->msglen = 0;
  //msleep(10); // provokes case where chan_send is called before any chan_recv
  while (1) {
    // receive a message
    uintptr_t msg;
    if (!ChanRecv(t->ch, &msg)) {
      //dlog("[recv_thread#%u] channel closed", t->id);
      break; // end thread when channel closed
    }

    // save received message for later inspection by test
    //dlog("T %u got %u", t->id, (u32)msg);
    assertf(t->msglen < t->msgcap,
      "[recv_thread#%u] received an excessive number of messages (>%u)",
      t->id, t->msgcap);
    t->msgv[t->msglen++] = msg;
    //dlog(">> %zu (recv_thread#%u)", (size_t)msg, t->id);

    // add some thread scheduling jitter
    // usleep(rand() % 10);
  }
  //LSemaSignal(t->sema, 1);
  //dlog("[recv_thread#%u] exit", t->id);
  return 0;
}


static void chan_1send_Nrecv(u32 bufcap, u32 nthreads, u32 nmessages);
// R_TEST(chan_1send_Nrecv_buffered1) { chan_1send_Nrecv(1, 2, 4); }
R_TEST(chan_1send_Nrecv_buffered1) { chan_1send_Nrecv(2, os_ncpu() + 1, 8); }
// R_TEST(chan_1send_Nrecv_buffered1) { chan_1send_Nrecv(1, 2, 4); }
// R_TEST(chan_1send_Nrecv_unbuffered) { chan_1send_Nrecv(0, 2, 4); }


static void chan_1send_Nrecv(u32 bufcap, u32 nthreads, u32 nmessages) {
  // serial sender, multiple receivers
  Mem mem = MemLibC();

  u32 send_message_count = nthreads * nmessages;
  TestThread* threads = memalloc(mem, sizeof(TestThread) * nthreads);

  // allocate storage for messages
  // the calling "sender" thread uses send_message_count messages while each
  // receiver thread is given send_message_count message slots for reception
  // as in theory one thread may receive all messages.
  size_t message_storage_count = send_message_count * (nthreads + 1);
  uintptr_t* message_storage = memalloc(mem, message_storage_count * sizeof(uintptr_t));
  uintptr_t* send_messages = &message_storage[0];

  // TODO: with buffered channels we need to sync recv threads with the sender
  // or else the sender may outrace the receiver to close and leave some messages
  // unreceived in the queue buffer.

  LSema sema;
  LSemaInit(&sema, 0);

  Chan* ch = ChanOpen(mem, /*cap*/bufcap);
  dlog("channel capacity: %u", ChanCap(ch));

  // init messages (1 2 3 ...)
  u64 send_message_sum = 0; // sum of all messages
  for (u32 i = 0; i < send_message_count; i++) {
    uintptr_t msg = (uintptr_t)i + 1; // make it 1-based for simplicity
    send_messages[i] = msg;
    send_message_sum += (u64)msg;
  }

  // init & spawn threads
  dlog("spawning %u threads", nthreads);
  for (u32 i = 0; i < nthreads; i++) {
    TestThread* t = &threads[i];
    t->id = i + 1;
    t->ch = ch;
    t->msgcap = send_message_count;
    t->msgv = &message_storage[(i + 1) * send_message_count];
    t->sema = &sema;
    auto status = thrd_create(&t->t, recv_thread, t);
    asserteq(status, thrd_success);
  }

  // send messages
  dlog("sending %u messages (values 1 2 3 ...)", send_message_count);
  for (u32 i = 0; i < send_message_count; i++) {
    uintptr_t msg = send_messages[i];
    ChanSend(ch, msg);
  }
  dlog("done sending %u messages", send_message_count);

  // msleep(100);

  // close the channel and wait for threads to exit
  ChanClose(ch);
  for (u32 i = 0; i < nthreads; i++) {
    auto t = &threads[i];
    int retval;
    thrd_join(t->t, &retval);
  }
  ChanFree(ch);

  // check results
  u32 recv_message_count = 0; // tally of total number of messages all threads received
  u64 recv_message_sum   = 0; // sum of all messages received
  for (u32 i = 0; i < nthreads; i++) {
    auto t = &threads[i];
    recv_message_count += t->msglen;
    for (u32 y = 0; y < t->msglen; y++) {
      recv_message_sum += (u64)t->msgv[y];
    }
  }
  asserteq(recv_message_count, send_message_count);
  asserteq(recv_message_sum, send_message_sum);

  memfree(mem, threads);
  memfree(mem, send_messages);
}


#endif /*R_TESTING_ENABLED*/
ASSUME_NONNULL_END
