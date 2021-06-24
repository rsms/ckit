#pragma once
ASSUME_NONNULL_BEGIN

// Chan is an optionally-buffered messaging channel for CSP-like processing.
typedef struct Chan Chan; // opaque

Chan* ChanOpen(Mem mem, u32 buffer_cap);
void ChanClose(Chan*);
void ChanFree(Chan*);

// ChanCap returns the channel's buffer capacity
u32 ChanCap(const Chan* c);

// ChanSend enqueues a message to a channel.
// Blocks until the message is sent or the channel is closed.
// Returns false if the channel closed.
bool ChanSend(Chan*, uintptr_t msg);

// ChanRecv dequeues a message from a channel.
// Blocks until there's a message available or the channel is closed.
// Returns true if a message was received, false if the channel is closed.
bool ChanRecv(Chan*, uintptr_t* msgptr_out);

// ChanTrySend attempts to sends a message without blocking.
// It returns true if the message was sent, false if not.
// Unlike ChanSend, this function does not return false to indicate that the channel
// is closed, but instead it returns false if the message was not sent and sets *closed
// to false if the reason for the failure was a closed channel.
bool ChanTrySend(Chan*, bool* closed, uintptr_t msg);

// ChanTryRecv works like ChanRecv but does not block.
// Returns true if a message was received.
// This function does not block/wait.
bool ChanTryRecv(Chan* ch, bool* closed, uintptr_t* result);

ASSUME_NONNULL_END
