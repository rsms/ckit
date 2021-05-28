#include "rbase.h"

#include <time.h>
#include <sys/time.h>
#if defined __APPLE__
#include <mach/mach_time.h>
#endif


void unixtime2(i64* sec, u64* nsec) {
  #ifdef CLOCK_REALTIME
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    *sec = (i64)ts.tv_sec;
    *nsec = (u64)ts.tv_nsec;
  #else
    struct timeval tv;
    gettimeofday(&tv, 0);
    *sec = (i64)tv.tv_sec;
    *nsec = ((u64)tv.tv_usec) * 1000;
  #endif
}


UnixTime unixtime() {
  #ifdef CLOCK_REALTIME
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return ts.tv_sec + (ts.tv_nsec * 1e-9);
  #else
    struct timeval tv;
    gettimeofday(&tv, 0);
    return tv.tv_sec + (tv.tv_usec * 1e-6);
  #endif
}


u64 unixtime_sec() {
  #ifdef CLOCK_REALTIME
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (u64)ts.tv_sec;
  #else
    struct timeval tv;
    gettimeofday(&tv, 0);
    return (u64)tv.tv_sec;
  #endif
}


// msleep sleeps for some number of milliseconds
// It may sleep for less time if a signal was delivered.
// Returns 0 when sleept for the requested time, -1 when interrupted.
int msleep(u64 milliseconds) {
  const u64 sec = milliseconds / 1000;
  struct timespec rqtp = {
    .tv_sec  = (long)sec,
    .tv_nsec = (milliseconds - (sec * 1000)) * 1000000,
  };
  return nanosleep(&rqtp, NULL);
}

int fmtduration(char* buf, int bufsize, u64 duration_ns) {
  const char* fmt = "%.0fns";
  double d = duration_ns;
  if (duration_ns >= 1000000000) {
    d /= 1000000000;
    fmt = "%.1fs";
  } else if (duration_ns >= 1000000) {
    d /= 1000000;
    fmt = "%.1fms";
  } else if (duration_ns >= 1000) {
    d /= 1000;
    fmt = "%.0fus";
  }
  return snprintf(buf, bufsize, fmt, d);
}


// nanotime returns nanoseconds measured from an undefined point in time
u64 nanotime(void) {
#if defined(__MACH__)
  static mach_timebase_info_data_t ti;
  static bool ti_init = false;
  if (!ti_init) {
    // note on atomicity: ok to do many times
    ti_init = true;
    UNUSED auto r = mach_timebase_info(&ti);
    assert(r == KERN_SUCCESS);
  }
  u64 t = mach_absolute_time();
  return (t * ti.numer) / ti.denom;
#elif defined(CLOCK_MONOTONIC)
  struct timespec ts;
  #ifdef NDEBUG
  clock_gettime(CLOCK_MONOTONIC, &ts);
  #else
  assert(clock_gettime(CLOCK_MONOTONIC, &ts) == 0);
  #endif
  return ((u64)(ts.tv_sec) * 1000000000) + ts.tv_nsec;
// #elif (defined _MSC_VER && (defined _M_IX86 || defined _M_X64))
  // TODO: QueryPerformanceCounter
#else
  struct timeval tv;
  #ifdef NDEBUG
  gettimeofday(&tv, nullptr);
  #else
  assert(gettimeofday(&tv, nullptr) == 0);
  #endif
  return ((u64)(tv.tv_sec) * 1000000000) + ((u64)(tv.tv_usec) * 1000);
#endif
}
