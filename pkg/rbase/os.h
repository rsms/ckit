#pragma once
#include "str.h"
ASSUME_NONNULL_BEGIN

// os_exepath returns the absolute path of the current executable
const char* os_exepath();

// os_stacktrace_fwrite writes a stacktrace (aka backtrace) to fp
void os_stacktrace_fwrite(FILE* nonull fp, int offset_frames);

// os_getcwd_str returns the current working directory in a newly allocated Str
Str os_getcwd_str();


ASSUME_NONNULL_END
