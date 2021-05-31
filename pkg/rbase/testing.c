#include "rbase.h"
#include <unistd.h>  // for isatty()


// -----------------------------------------------------------------------------------
#if defined(R_TESTING_MAIN_IMPL)

int main(int argc, const char** argv) {
  return testing_main(argc, argv);
}


// // -----------------------------------------------------------------------------------
// #elif defined(R_TESTING_INIT_IMPL)
// This is unreliable; tests are constructors too and may be called after this one.
//
// // constructor initializer calls testing_main with env R_TEST_FILTER as first arg
// __attribute__((constructor,used)) static void testing_init() {
//   static bool initialized = false;
//   if (initialized)
//     return;
//   initialized = true;
//   const char* argv[2] = { "", "" };
//   int argc = 1;
//   auto arg1 = getenv("R_TEST_FILTER");
//   if (arg1) {
//     argv[1] = arg1;
//     argc++;
//   }
//   exit(testing_main(argc, argv));
// }


// -----------------------------------------------------------------------------------
#elif R_TESTING_ENABLED


static Testing* testlist_head = NULL;
static Testing* testlist_tail = NULL;

static bool stderr_isatty = false;
static long stderr_fpos = 0;
static const char* waitstyle = "";
static const char* okstyle = "";
static const char* failstyle = "";
static const char* dimstyle = "";
static const char* nostyle = "";


void testing_add_test(Testing* t) {
  t->file = path_cwdrel(t->file);
  if (testlist_head == NULL) {
    testlist_head = t;
  } else {
    testlist_tail->_next = t;
  }
  testlist_tail = t;
}


static bool should_run_test(Testing* t, const char* nullable filter_prefix) {
  if (!filter_prefix)
    return true; // no filter
  size_t len = strlen(filter_prefix);
  return (len <= strlen(t->name) && memcmp(filter_prefix, t->name, len) == 0);
}

static void print_status(Testing* t, bool done, const char* msg) {
  const char* marker_wait = "• ";
  const char* marker_ok   = "✓ ";
  const char* marker_fail = "✗ ";
  if (!stderr_isatty) {
    marker_wait = "";
    marker_ok   = "OK ";
    marker_fail = "FAIL ";
  }
  const char* status = done ? (t->failed ? marker_fail : marker_ok) : marker_wait;
  const char* style = done ? (t->failed ? failstyle : okstyle) : waitstyle;
  fprintf(stderr, "TEST %s%s%s%s %s%s:%d%s %s\n",
    style, status, t->name, nostyle,
    dimstyle, t->file, t->line, nostyle,
    msg);
}

static void testrun_start(Testing* t) {
  print_status(t, false, "...");
  if (stderr_isatty)
    stderr_fpos = ftell(stderr);
}

static void testrun_end(Testing* t, u64 startat) {
  auto timespent = nanotime() - startat;
  if (stderr_isatty) {
    long fpos = ftell(stderr);
    if (fpos == stderr_fpos) {
      // nothing has been printed since _testing_start_run; clear line
      // \33[A    = move to previous line
      // \33[2K\r = clear line
      fprintf(stderr, "\33[A\33[2K\r");
    }
  }
  char durbuf[128];
  fmtduration(durbuf, sizeof(durbuf), timespent);
  print_status(t, true, durbuf);
}


int testing_main(int argc, const char** argv) {
  // usage: $0 [filter_prefix]
  // note: if env R_TEST_FILTER is set, it is used as the default value for argv[1]

  if (!testlist_head) {
    fprintf(stderr, "No tests registered. Define tests with R_TEST(name){body...}\n");
    return 0;
  }

  stderr_isatty = isatty(2);
  if (stderr_isatty) {
    waitstyle  = "";
    okstyle    = "\e[1;32m"; // green
    failstyle  = "\e[1;31m"; // red
    dimstyle   = "\e[2m";
    nostyle    = "\e[0m";
  }

  const char* filter_prefix = getenv("R_TEST_FILTER");
  if (argc > 1 && strlen(argv[1]) > 0)
    filter_prefix = argv[1];

  int failcount = 0;
  int runcount = 0;
  for (Testing* t = testlist_head; t; t = t->_next) {
    if (should_run_test(t, filter_prefix)) {
      u64 startat = nanotime();
      testrun_start(t);
      t->fn(t);
      testrun_end(t, startat);
      runcount++;
      if (t->failed)
        failcount++;
    }
  }

  if (runcount == 0) {
    assertnotnull(filter_prefix);
    fprintf(stderr, "no tests with prefix %s\n", filter_prefix);
    return 0;
  }

  Str progname = path_base_append(str_new(128), argv[0]);

  if (failcount > 0) {
    fprintf(stderr, "%sFAILED:%s %s (%d)\n",
      failstyle, nostyle, progname, failcount);
    for (Testing* t = testlist_head; t; t = t->_next) {
      if (t->failed)
        fprintf(stderr, "  %s\tat %s:%d\n", t->name, t->file, t->line);
    }
  // } else {
  //   fprintf(stderr, "%sPASS:%s %s\n", okstyle, nostyle, progname);
  }

  str_free(progname);

  return failcount > 0 ? 1 : 0;
}


// #ifndef R_TEST_ENV_NAME
//   #define R_TEST_ENV_NAME "R_TEST"
// #endif
// // constructor initializer calls testing_main if getenv(R_TEST_ENV_NAME) is set
// #if R_TESTING_ENABLED
// __attribute__((constructor,used)) static void testing_init() {
//   static bool initialized = false;
//   if (initialized)
//     return;
//   initialized = true;
//   on = false;
//   dlog("testing_init");
//   auto s = getenv(R_TEST_ENV_NAME);
//   if (!s || strcmp(s, "0") == 0)
//     return;
//   const char* argv[2] = { "", "" };
//   int argc = 1;
//   if (strcmp(s, "1") != 0) {
//     // test name prefix
//     argv[1] = s;
//     argc++;
//   }
//   testing_main(argc, argv);
// }
// #endif

#endif /* R_TEST_MAIN, R_TESTING_ENABLED */
