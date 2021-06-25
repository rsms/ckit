#include "rbase.h"
#include <sys/resource.h> // rusage

ASSUME_NONNULL_BEGIN

typedef struct Timer {
  u64 cycles;
  u64 clock;
  u64 time;
  u64 utime;

  // temporary state
  struct rusage ru_start;
  u64 cycles_start;
  u64 cycles_prev;
} Timer;


inline static u64 read_rdtsc()
#if defined(__i386__)
{
  u64 x;
  __asm__ volatile (".byte 0x0f, 0x31" : "=A" (x));
  return x;
}
#elif defined(__x86_64__)
{
  u32 hi, lo;
  __asm__ __volatile__ ("rdtsc" : "=a"(lo), "=d"(hi));
  return ( (u64)lo ) | ( ((u64)hi)<<32 );
}
// #elif defined(__arm__)  // ARMv7
//   asm volatile( "mrc p15, 0, %0, c9, c13, 0" : "=r"(a) );
#elif defined(__powerpc__)
{
  u64 result = 0;
  unsigned long int upper, lower, tmp;
  __asm__ volatile(
                "0:              \n"
                "\tmftbu   %0    \n"
                "\tmftb    %1    \n"
                "\tmftbu   %2    \n"
                "\tcmpw    %2,%0 \n"
                "\tbne     0b    \n"
                : "=r"(upper),"=r"(lower),"=r"(tmp)
                );
  result = (u64)((upper << 32) | lower);
  return result;
}
#endif


inline static Timer TimerStart() {
  Timer t = {0};
  t.time = nanotime(); // ≈ clock_gettime(CLOCK_MONOTONIC)
  getrusage(RUSAGE_SELF, &t.ru_start);
  return t;
}

ALWAYS_INLINE static void TimerCycleSampleStart(Timer* t) {
  t->cycles_start = read_rdtsc();
}

ALWAYS_INLINE static void TimerCycleSampleStop(Timer* t) {
  u64 cycles = read_rdtsc();
  if (cycles > t->cycles_start)
    t->cycles_prev = cycles - t->cycles_start;
  // else: counter flipped around; use previous value
  t->cycles += t->cycles_prev;
}

inline static void TimerStop(Timer* t) {
  //t->clock = clock() - t->clock;
  t->time = nanotime() - t->time;
  struct rusage ru_end;
  getrusage(RUSAGE_SELF, &ru_end);
  u64 sec = (u64)(ru_end.ru_utime.tv_sec - t->ru_start.ru_utime.tv_sec);
  u64 usec = (u64)(ru_end.ru_utime.tv_usec - t->ru_start.ru_utime.tv_usec);
  t->utime = (usec + (sec * 1000000)) * 1000;
}

// static void TimerLogv(const Timer t, u32 numops, const char* fmt, va_list ap) {
//   vfprintf(stderr, fmt, ap);
//
//   char sumtime_buf[20];
//   char optime_buf[20];
//   fmtduration(sumtime_buf, sizeof(sumtime_buf), t.time);
//   fmtduration(optime_buf, sizeof(optime_buf), t.time / (u64)numops);
//
//   fprintf(stderr,
//     "%u ops, %s (%s/op), " FMT_U64 " cycles (" FMT_U64 " cycles/op)\n",
//     numops, sumtime_buf, optime_buf, t.cycles, t.cycles / (u64)numops);
// }

// static void TimerLog(const Timer t, u32 numops, const char* fmt, ...) {
//   va_list ap;
//   va_start(ap, fmt);
//   TimerLogv(t, numops, fmt, ap);
//   va_end(ap);
// }

typedef struct Benchmark Benchmark;
typedef Timer(*BenchFun)(Benchmark*);
struct Benchmark {
  // inputs set by the R_BENCHMARK macro
  const char* name;
  const char* file;
  int         line;
  BenchFun    fn;

  // optional callbacks called before and after all samples
  void(*onbegin)(Benchmark*);
  void(*onend)(Benchmark*);
  uintptr_t userdata; // anything you'd like; not used by the benchmark framework

  // variables used by the benchmark framework
  // N is the number of "operations" a test is expected to perform.
  // Its value may vary from call to call.
  size_t N; // Read-only by benchmark function

  // N_divisor is an optional number to divide N by when calculating the time/op statistic.
  // It defaults to 1 but can be set to a larger number by a benchmark that may perform
  // multiple logical operations per N iteration.
  size_t N_divisor;

  // iteration is a read-only value which starts at 0 and increments by one
  // for each invocation of the benchmark function fn.
  // It can be used to do some initial setup e.g. "if (b->iteration == 0) { ... }"
  size_t iteration;

  // internal
  Benchmark* _next;
};

static Benchmark* _benchmarks_head = NULL;
static Benchmark* _benchmarks_tail = NULL;
static bool stderr_isatty = false;

static void benchmark_add(Benchmark* b) {
  if (_benchmarks_tail) {
    _benchmarks_tail->_next = b;
  } else {
    _benchmarks_head = b;
  }
  _benchmarks_tail = b;
}

#define R_BENCHMARK(NAME, /* [, onbegin[, onend]] */...)                              \
  static Timer bench_##NAME(Benchmark* b);                                            \
  __attribute__((constructor,used)) static void bench_##NAME##_() {                   \
    static Benchmark b = { #NAME, __FILE__, __LINE__, &bench_##NAME, ##__VA_ARGS__ }; \
    benchmark_add(&b);                                                                \
  }                                                                                   \
  static Timer bench_##NAME


static void benchmark_run(Benchmark* b, u64 maxtime_ms) {
  fprintf(stderr, "\nSTART %s ", b->name);
  fputc((stderr_isatty && b->onbegin) ? ' ' : '\n', stderr);
  if (b->onbegin) {
    // make sure this is some deterministic number in case the callback uses it
    b->N = 1;
    long stderr_fpos = 0;
    if (stderr_isatty)
      stderr_fpos = ftell(stderr);
    b->onbegin(b);
    if (stderr_isatty && ftell(stderr) == stderr_fpos) {
      // onbegin did not write to stderr,
      // so let's terminate the "start" message with a line break.
      fputc('\n', stderr);
    }
  }

  u64 cycles_total = 0;
  u64 utime_total = 0;
  u64 time_total = 0;
  u64 time_stop = 1000 * 1000 * maxtime_ms; // ms -> ns
  size_t niterations = 0;
  u64 N_total = 0;

  // if an iteration takes less than this, increase N
  u64 time_N_bump_threshold = 1000 * 1000; // 1ms

  // start with 1000 iterations
  b->N = 1000;
  b->iteration = 0;

  while (time_total < time_stop) {
    auto timer = b->fn(b); // TODO
    if (timer.time < time_N_bump_threshold) {
      // the iteration was too quick; increase N for the next run
      b->N = b->N * 10;
    }
    N_total += b->N;
    time_total += timer.time;
    utime_total += timer.utime;
    cycles_total += timer.cycles;
    niterations++;
    // b->iteration = niterations;
    b->iteration++;
    // sched_yield();
  }

  if (b->N_divisor > 1)
    N_total = N_total * b->N_divisor;

  // u64 clock_avg_ns = (u64)(
  //   ((double)clock_total / (double)N_total) / CLOCKS_PER_SEC * 1000000000.0
  // );

  char sumtime_buf[20];
  char optime_buf[20];
  char oputime_buf[20];
  fmtduration(sumtime_buf, sizeof(sumtime_buf), time_total);
  fmtduration(optime_buf, sizeof(optime_buf), time_total / N_total);
  fmtduration(oputime_buf, sizeof(oputime_buf), utime_total / N_total);

  // fprintf(stderr,
  //   "END   %s: " FMT_U64 " ops in %s (avg real %s/op, user %s/op)",
  //   b->name, N_total, sumtime_buf, optime_buf, oputime_buf);
  fprintf(stderr,
    "END   %s: " FMT_U64 " ops in %s (avg %s/op",
    b->name, N_total, sumtime_buf, optime_buf);
  if (cycles_total > 0) {
    fprintf(stderr, ", " FMT_U64 " cycles/op)\n", cycles_total / N_total);
  } else {
    fprintf(stderr, ")\n");
  }
  if (b->onend)
    b->onend(b);
}


static int benchmark_main(int argc, const char** argv) {
  u64 maxtime_ms = 1000;//ms
  stderr_isatty = isatty(2);

  if (argc > 1) {
    u64 n = 0;
    if (parseu64(argv[1], strlen(argv[1]), 10, &n) && n > 0)
      maxtime_ms = n;
  }

  Benchmark* b = _benchmarks_head;
  while (b) {
    benchmark_run(b, maxtime_ms);
    b = b->_next;
  }
  return 0;
}


ASSUME_NONNULL_END
