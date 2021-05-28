#pragma once
ASSUME_NONNULL_BEGIN

typedef double UnixTime;

// unixtime returns the current real time as a UNIX timestamp in high precision.
// Number of seconds since Jan 1 1970 UTC.
UnixTime unixtime();

// unixtime_sec returns the current real time as a UNIX timestamp with second precision.
u64 unixtime_sec();

// unixtime2 returns the second and nanasecond parts as two integers
void unixtime2(i64* sec, u64* nsec);

// nanotime returns nanoseconds measured from an undefined point in time.
// It uses the most high-resolution, low-latency clock available on the system.
u64 nanotime();

// msleep sleeps for some number of milliseconds
// It may sleep for less time if a signal was delivered.
// Returns 0 when sleept for the requested time, -1 when interrupted.
int msleep(u64 milliseconds);

// fmtduration appends human-readable time duration to buf
int fmtduration(char* buf, int bufsize, u64 duration_ns);


ASSUME_NONNULL_END
