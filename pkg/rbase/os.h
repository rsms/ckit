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


ASSUME_NONNULL_END
