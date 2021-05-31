#include "rbase.h"



__attribute__((noinline))
static void bar() {
  panic("lolcat");
}

__attribute__((noinline))
static void foo() {
  bar();
}

R_TEST(boom) {
  foo();
}


// // writeline calls write(fd, ptr, len); prints a newline unless ptr[len-1]=='\n'
// // Does not write anything if len==0.
// void writeline(int fd, const void* ptr, size_t len) {
//   if (len > 0) {
//     write(fd, ptr, len);
//     if (((char*)ptr)[len-1] != '\n')
//       write(fd, "\n", 1);
//   }
// }

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

  fprintf(stderr, "\npanic: ");

  va_list ap;
  va_start(ap, fmt);
  vfprintf(stderr, fmt, ap);
  va_end(ap);

  fprintf(stderr, " in %s at %s:%d\n", fname, filename, lineno);

  os_stacktrace_fwrite(stderr, /* offsetFrames = */ 1);

  funlockfile(fp);
  fflush(fp);

  // exit(2);
  abort();
  // _Exit(2);
}
