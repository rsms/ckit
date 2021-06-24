#include "rbase.h"

static atomic_i32 _stacktrace_limit = 30;
static atomic_i32 _stacktrace_limit_src = 5;

void panic_set_stacktrace_limits(int limit, int limit_src) {
  AtomicStore(&_stacktrace_limit, limit);
  AtomicStore(&_stacktrace_limit_src, limit_src);
}

void panic_get_stacktrace_limits(int* limit, int* limit_src) {
  *limit = AtomicLoad(&_stacktrace_limit);
  *limit_src = AtomicLoad(&_stacktrace_limit_src);
}

void _errlog(const char* fmt, ...) {
  FILE* fp = stderr;
  flockfile(fp);

  va_list ap;
  va_start(ap, fmt);
  vfprintf(fp, fmt, ap);
  va_end(ap);
  int err = errno;
  if (err != 0) {
    errno = 0;
    char buf[256];
    if (strerror_r(err, buf, countof(buf)) == 0)
      fprintf(fp, " ([%d] %s)\n", err, buf);
  } else {
    fputc('\n', fp);
  }

  funlockfile(fp);
  fflush(fp);
}

_Noreturn void _panic(const char* filename, int lineno, const char* fname, const char* fmt, ...) {
  filename = path_cwdrel(filename);
  FILE* fp = stderr;
  flockfile(fp);

  // panic: {message} in {function} at {source_location}
  fprintf(stderr, "\npanic: ");
  va_list ap;
  va_start(ap, fmt);
  vfprintf(stderr, fmt, ap);
  va_end(ap);
  fprintf(stderr, " in %s at %s:%d\n", fname, filename, lineno);

  // errno: [{code}] {message}
  if (errno != 0)
    fprintf(stderr, "errno: [%d] %s\n", errno, strerror(errno));

  // stack trace
  const int offsetFrames = 1;
  int limit = 0;
  int limit_src = 0;
  panic_get_stacktrace_limits(&limit, &limit_src);
  os_stacktrace_fwrite(stderr, offsetFrames, limit, limit_src);

  funlockfile(fp);
  fflush(fp);

  fsync(STDERR_FILENO);

  // exit(2);
  abort();
  // _Exit(2);
}
