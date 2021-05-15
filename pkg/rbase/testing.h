// unit testing
//   R_TESTING_ENABLED  defined to 1 when unit testing is enabled
//   R_TEST(name)body   defines a unit test to be run before main()
//
// Example:
//
//   R_TEST(foo) {
//     assert(1+2 == 3);
//   }
//
#pragma once
ASSUME_NONNULL_BEGIN

// #ifdef R_TESTING_ENABLED
//   #undef R_TESTING_ENABLED
//   #define R_TESTING_ENABLED 1
// #elif defined(DEBUG) && !defined(NDEBUG)
//   #define R_TESTING_ENABLED 1
// #endif

#ifdef R_TESTING_ENABLED
  // Testing holds information about a specific unit test. R_TEST creates it for you.
  // If you use testing_add_test you'll need to provide it yourself.
  typedef struct Testing Testing;
  typedef void(*TestingFunc)(Testing*);
  typedef struct Testing {
    const char* name;
    const char* file;
    int         line;
    TestingFunc fn;
    bool        failed; // set this to true to signal failure
    Testing*    _next;
  } Testing;

  // testing_main runs all test defined with R_TEST or manually added with testing_add_test.
  //   usage: $0 [filter_prefix]
  //   filter_prefix   If given, only run tests which name has this prefix.
  int testing_main(int argc, const char* argv[argc+1]);

  // testing_add_test explicity adds a test
  void testing_add_test(Testing*);

  #define R_TEST(NAME)                                                \
    static void NAME##_test(Testing*);                                \
    __attribute__((constructor,used)) static void NAME##_test_() {    \
      static Testing t = { #NAME, __FILE__, __LINE__, &NAME##_test }; \
      testing_add_test(&t);                                           \
    }                                                                 \
    static void NAME##_test(Testing* unittest)

  // #ifdef NDEBUG
  //   #warning "R_TESTING_ENABLED is enabled while NDEBUG is defined; tests may fail"
  // #endif

  // make sure "#if R_TESTING_ENABLED" works
  #undef R_TESTING_ENABLED
  #define R_TESTING_ENABLED 1
#else
  #warning "R_TESTING_ENABLED is not set"
  #define R_TEST(name) \
    __attribute__((unused)) inline static void name##_test()
  #define testing_main(argc, argv) 0
#endif /* defined(R_TESTING_ENABLED) */


ASSUME_NONNULL_END
