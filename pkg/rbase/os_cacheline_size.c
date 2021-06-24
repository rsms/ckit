#include "rbase.h"

#if defined __APPLE__
  #include <sys/sysctl.h>
#elif defined __linux__
  #include <stdio.h>
#elif (defined(__FreeBSD__) || defined(__NetBSD__))
  #include <sys/param.h>
#else
  // TODO: Windows
  #error Usupported platform
#endif

// Note: Most x86_64 and arm64 archs have a 64-byte cache line size,
// so default to that in case of syscall error.
#define CACHE_LINE_SIZE_FALLBACK 64

ASSUME_NONNULL_BEGIN

u32 os_cacheline_size() {
#if (defined(__FreeBSD__) || defined(__NetBSD__))
  // just return the constant value
  return (size_t)CACHE_LINE_SIZE;
#else
  static size_t cache_line_size = 0;
  static r_sync_once_flag onceflag = {0};
  r_sync_once(&onceflag, {
    #if defined __APPLE__
      size_t z = sizeof(cache_line_size);
      if (sysctlbyname("hw.cachelinesize", &cache_line_size, &z, 0, 0) != 0)
        cache_line_size = CACHE_LINE_SIZE_FALLBACK;
    #elif defined __linux__
      FILE *p = NULL;
      p = fopen("/sys/devices/system/cpu/cpu0/cache/index0/coherency_line_size", "r");
      if (p) {
        if (fscanf(p, "%zu", &cache_line_size) != 1)
          cache_line_size = CACHE_LINE_SIZE_FALLBACK;
        fclose(p);
      } else {
        cache_line_size = CACHE_LINE_SIZE_FALLBACK;
      }
    #endif
  });
  return (u32)cache_line_size;
#endif
}

ASSUME_NONNULL_END
