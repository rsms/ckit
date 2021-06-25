#pragma once
ASSUME_NONNULL_BEGIN

// Chan is an optionally-buffered messaging channel for CSP-like processing.
// Example:
//
//   Chan* c = ChanOpen(mem, sizeof(int), 4);
//
//   int send_messages[] = { 123, 456 };
//   ChanSend(c, &send_messages[0]);
//   ChanSend(c, &send_messages[1]);
//
//   int recv_messages[] = { 0, 0 };
//   ChanRecv(c, &recv_messages[0]);
//   ChanRecv(c, &recv_messages[1]);
//
//   assert(recv_messages[0] == send_messages[0]);
//   assert(recv_messages[1] == send_messages[1]);
//
//   ChanClose(c);
//   ChanFree(c);
//
typedef struct Chan Chan; // opaque

// ChanOpen creates and initializes a new channel which holds elements of elemsize byte.
// If bufcap>0 then a buffered channel with the capacity to hold bufcap elements is created.
Chan* ChanOpen(Mem mem, size_t elemsize, u32 bufcap);

// ChanClose cancels any waiting senders and receivers.
// Messages sent before this call are guaranteed to be delivered, assuming there are
// active receivers. Once a channel is closed it can not be reopened nor sent to.
// ChanClose must only be called once per channel.
void ChanClose(Chan*);

// ChanFree frees memory of a channel
void ChanFree(Chan*);

// ChanCap returns the channel's buffer capacity
u32 ChanCap(const Chan* c);

// ChanSend enqueues a message to a channel by copying the value at elemptr to the channel.
// Blocks until the message is sent or the channel is closed.
// Returns false if the channel closed.
bool ChanSend(Chan*, void* elemptr);

// ChanRecv dequeues a message from a channel by copying a received value to elemptr.
// Blocks until there's a message available or the channel is closed.
// Returns true if a message was received, false if the channel is closed.
bool ChanRecv(Chan*, void* elemptr);

// ChanTrySend attempts to sends a message without blocking.
// It returns true if the message was sent, false if not.
// Unlike ChanSend, this function does not return false to indicate that the channel
// is closed, but instead it returns false if the message was not sent and sets *closed
// to false if the reason for the failure was a closed channel.
bool ChanTrySend(Chan*, bool* closed, void* elemptr);

// ChanTryRecv works like ChanRecv but does not block.
// Returns true if a message was received.
// This function does not block/wait.
bool ChanTryRecv(Chan* ch, bool* closed, void* elemptr);

ASSUME_NONNULL_END
