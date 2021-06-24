#pragma once
#include "str.h"
ASSUME_NONNULL_BEGIN

// os_exepath returns the absolute path of the current executable
const char* os_exepath();

// os_stacktrace_fwrite writes a stacktrace (aka backtrace) to fp.
// limit is the total limit. A negative value means "unlimited".
// limit_src limits how many source traces are printed. A negative value means "unlimited".
void os_stacktrace_fwrite(FILE* nonull fp, int offset_frames, int limit, int limit_src);

// os_getcwd_str returns the current working directory in a newly allocated Str
Str os_getcwd_str();

// os_ncpu returns the number of hardware threads.
// Returns 0 when the number of CPUs could not be determined.
u32 os_ncpu();

// os_cacheline_size returns the system's CPU "cache line" size in bytes (usually 64)
u32 os_cacheline_size();

// os_pagesize returns the system's memory page size, which is usually 4096 bytes.
// This function returns a cached value from memory, read from the OS at init.
inline static size_t os_pagesize() { return mem_pagesize(); }

// os_user_home_dir returns the current user's home directory.
// Returns "" on failure.
const char* os_user_home_dir();


ASSUME_NONNULL_END
