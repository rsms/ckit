#pragma once
//
// void errlog(const char* fmt, ...)
// void panic(const char* fmt, ...)
// void stacktrace_fwrite(FILE* fp, int offset_frames)
//
// void _errlog(const char* fmt, ...)
// _Noreturn void _panic(const char* file, int line, const char* func, const char* fmt, ...);
//
ASSUME_NONNULL_BEGIN

#ifdef DEBUG
  #define errlog(fmt, ...) _errlog(fmt " (%s:%d)", ##__VA_ARGS__, path_cwdrel(__FILE__), __LINE__)
  #define TODO_IMPL   panic("\e[1;33mTODO_IMPL %s\e[0m\n", __PRETTY_FUNCTION__)
  #ifdef __builtin_unreachable
    #define UNREACHABLE ({ panic("\e[1;31mUNREACHABLE\e[0m"); __builtin_unreachable(); })
  #else
    #define UNREACHABLE panic("\e[1;31mUNREACHABLE\e[0m")
  #endif
#else
  #define errlog(fmt, ...) _errlog(fmt, ##__VA_ARGS__)
  #define TODO_IMPL   abort()
  #ifdef __builtin_unreachable
    #define UNREACHABLE __builtin_unreachable()
  #else
    #define UNREACHABLE abort()
  #endif
#endif

// panic prints msg (and errno, if non-zero) to stderr along with a stack trace and calls abort()
#define panic(fmt, ...) _panic(__FILE__, __LINE__, __FUNCTION__, fmt, ##__VA_ARGS__)

// panic_get_stacktrace_limits reads the current stack trace limits
void panic_get_stacktrace_limits(int* limit, int* limit_src);

// panic_set_stacktrace_limits sets new stack trace limits.
// `limit` limits the total number of stack frames reported while `limit_src` limits the
// number of source traces printed. If limit_src is capped to the value of limit.
// A negative limit means "unlimited".
void panic_set_stacktrace_limits(int limit, int limit_src);

// --------------------------------------------------------------------------------------------
// implementation

void _errlog(const char* fmt, ...) ATTR_FORMAT(printf, 1, 2);

_Noreturn void _panic(
  const char* filename, int lineno, const char* fname, const char* fmt, ...)
  ATTR_FORMAT(printf, 4, 5);

ASSUME_NONNULL_END
