#pragma once
#if defined(__gnu_linux__) || defined(__linux__)
  #define _GNU_SOURCE 1
#endif
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <inttypes.h>
#include <limits.h>
#include <errno.h>
#include <sys/types.h>
#ifndef __cplusplus
  #include <stdatomic.h>
#endif

#include "defs_target.h"

typedef signed char            i8;
typedef unsigned char          u8;
typedef signed short int       i16;
typedef unsigned short int     u16;
typedef signed int             i32;
typedef unsigned int           u32;
typedef signed long long int   i64;
typedef unsigned long long int u64;
typedef float                  f32;
typedef double                 f64;
typedef unsigned int           uint;
typedef unsigned long          size_t;
typedef signed long            ssize_t;
typedef unsigned long          uintptr_t;
typedef signed long            intptr_t;

// compiler feature test macros
#ifndef __has_attribute
  #define __has_attribute(x)  0
#endif
#ifndef __has_extension
  #define __has_extension   __has_feature
#endif
#ifndef __has_feature
  #define __has_feature(x)  0
#endif
#ifndef __has_include
  #define __has_include(x)  0
#endif
#ifndef __has_builtin
  #define __has_builtin(x)  0
#endif

// nullability
#if defined(__clang__) && __has_feature(nullability)
  #define ASSUME_NONNULL_BEGIN _Pragma("clang assume_nonnull begin")
  #define ASSUME_NONNULL_END   _Pragma("clang assume_nonnull end")
  #define __NULLABILITY_PRAGMA_PUSH _Pragma("clang diagnostic push")  \
    _Pragma("clang diagnostic ignored \"-Wnullability-completeness\"")
  #define __NULLABILITY_PRAGMA_POP _Pragma("clang diagnostic pop")
#else
  #define _Nullable
  #define _Nonnull
  #define _Null_unspecified
  #define __NULLABILITY_PRAGMA_PUSH
  #define __NULLABILITY_PRAGMA_POP
  #define ASSUME_NONNULL_BEGIN
  #define ASSUME_NONNULL_END
#endif
#define nullable      _Nullable
#define nonull        _Nonnull
#define nonnullreturn __attribute__((returns_nonnull))

#define _DIAGNOSTIC_IGNORE_PUSH(x) _Pragma("GCC diagnostic push") _Pragma(#x)
#define DIAGNOSTIC_IGNORE_PUSH(x)  _DIAGNOSTIC_IGNORE_PUSH(GCC diagnostic ignored #x)
#define DIAGNOSTIC_IGNORE_POP      _Pragma("GCC diagnostic pop")

#ifdef __cplusplus
  #define NORETURN noreturn
#else
  #define NORETURN      _Noreturn
  #define auto          __auto_type
  #define static_assert _Static_assert
#endif

#if __has_attribute(fallthrough)
  #define FALLTHROUGH __attribute__((fallthrough))
#else
  #define FALLTHROUGH
#endif

#if __has_attribute(musttail)
  #define R_MUSTTAIL __attribute__((musttail))
#else
  #define R_MUSTTAIL
#endif

#ifndef thread_local
  #define thread_local _Thread_local
#endif

#ifdef __cplusplus
  #define EXTERN_C extern "C"
#else
  #define EXTERN_C
#endif

// R_UNLIKELY(integralexpr)->integralexpr
// Provide explicit branch prediction. Use like this:
// if (R_UNLIKELY(buf & 0xff))
//   error_hander("error");
// Caution! Use with care. You are probably going to make the wrong assumption.
// From the GCC manual:
//   In general, you should prefer to use actual profile feedback for this (-fprofile-arcs),
//   as programmers are notoriously bad at predicting how their programs actually perform.
//   However, there are applications in which this data is hard to collect.
#ifdef __builtin_expect
  #define R_UNLIKELY(x) __builtin_expect((x), 0)
#else
  #define R_UNLIKELY(x) (x)
#endif

// ATTR_FORMAT(archetype, string-index, first-to-check)
// archetype determines how the format string is interpreted, and should be printf, scanf,
// strftime or strfmon.
// string-index specifies which argument is the format string argument (starting from 1),
// while first-to-check is the number of the first argument to check against the format string.
// For functions where the arguments are not available to be checked (such as vprintf),
// specify the third parameter as zero.
#if __has_attribute(format)
  #define ATTR_FORMAT(...) __attribute__((format(__VA_ARGS__)))
#else
  #define ATTR_FORMAT(...)
#endif

#if __has_attribute(always_inline)
  #define ALWAYS_INLINE __attribute__((always_inline)) inline
#else
  #define ALWAYS_INLINE inline
#endif

#if __has_attribute(noinline)
  #define NO_INLINE __attribute__((noinline))
#else
  #define NO_INLINE
#endif

#if __has_attribute(unused)
  #define UNUSED __attribute__((unused))
#else
  #define UNUSED
#endif

#if __has_attribute(warn_unused_result)
  #define WARN_UNUSED_RESULT __attribute__((warn_unused_result))
#else
  #define WARN_UNUSED_RESULT
#endif

#if __has_feature(address_sanitizer)
  // https://clang.llvm.org/docs/AddressSanitizer.html
  #define ASAN_ENABLED 1
  #define ASAN_DISABLE_ADDR_ATTR __attribute__((no_sanitize("address"))) /* function attr */
#else
  #define ASAN_DISABLE_ADDR_ATTR
#endif

#ifndef offsetof
  #define offsetof(st, m) ((size_t)&(((st*)0)->m))
#endif

#define countof(x) \
  ((sizeof(x)/sizeof(0[x])) / ((size_t)(!(sizeof(x) % sizeof(0[x])))))

#define MAX(a,b) \
  ({__typeof__ (a) _a = (a); __typeof__ (b) _b = (b); _a > _b ? _a : _b; })
  // turns into CMP + CMOV{L,G} on x86_64
  // turns into CMP + CSEL on arm64

#define MIN(a,b) \
  ({__typeof__ (a) _a = (a); __typeof__ (b) _b = (b); _a < _b ? _a : _b; })
  // turns into CMP + CMOV{L,G} on x86_64
  // turns into CMP + CSEL on arm64

// division of integer, rounding up
#define r_idiv_ceil(x, y) (1 + (((x) - 1) / (y)))

// portable type formatters
#define FMT_S64 "%" PRId64  // signed decimal
#define FMT_U64 "%" PRIu64  // unsigned decimal
#define FMT_O64 "%" PRIo64  // unsigned octal
#define FMT_X64 "%" PRIX64  // unsigned uppercase hexadecimal
#define FMT_x64 "%" PRIx64  // unsigned lowercase hexadecimal

// r_type_formatter yields a printf formatting pattern for the type of x
#define r_type_formatter(x) _Generic((x), \
  unsigned long long: FMT_U64, \
  unsigned long:      "%lu", \
  unsigned int:       "%u", \
  long long:          PRId64, \
  long:               "%ld", \
  int:                "%d", \
  char:               "%c", \
  unsigned char:      "%C", \
  const char*:        "%s", \
  char*:              "%s", \
  bool:               "%d", \
  float:              "%f", \
  double:             "%f", \
  void*:              "%p", \
  const void*:        "%p", \
  default:            "%p" \
)

// T align2<T>(T x, T y) rounds up n to closest boundary w (w must be a power of two)
//
// E.g.
//   align(0, 4) => 0
//   align(1, 4) => 4
//   align(2, 4) => 4
//   align(3, 4) => 4
//   align(4, 4) => 4
//   align(5, 4) => 8
//   ...
//
#define align2(n,w) ({ \
  assert(((w) & ((w) - 1)) == 0); /* alignment w is not a power of two */ \
  ((n) + ((w) - 1)) & ~((w) - 1); \
})

// UNCONST_TYPEOF(v) yields __typeof__ without const qualifier (for basic types only)
#define UNCONST_TYPEOF(x)                                     \
  __typeof__(_Generic((x),                                    \
    signed char:              ({ signed char        _; _; }), \
    const signed char:        ({ signed char        _; _; }), \
    unsigned char:            ({ unsigned char      _; _; }), \
    const unsigned char:      ({ unsigned char      _; _; }), \
    short:                    ({ short              _; _; }), \
    const short:              ({ short              _; _; }), \
    unsigned short:           ({ unsigned short     _; _; }), \
    const unsigned short:     ({ unsigned short     _; _; }), \
    int:                      ({ int                _; _; }), \
    const int:                ({ int                _; _; }), \
    unsigned:                 ({ unsigned           _; _; }), \
    const unsigned:           ({ unsigned           _; _; }), \
    long:                     ({ long               _; _; }), \
    const long:               ({ long               _; _; }), \
    unsigned long:            ({ unsigned long      _; _; }), \
    const unsigned long:      ({ unsigned long      _; _; }), \
    long long:                ({ long long          _; _; }), \
    const long long:          ({ long long          _; _; }), \
    unsigned long long:       ({ unsigned long long _; _; }), \
    const unsigned long long: ({ unsigned long long _; _; }), \
    float:                    ({ float              _; _; }), \
    const float:              ({ float              _; _; }), \
    double:                   ({ double             _; _; }), \
    const double:             ({ double             _; _; }), \
    long double:              ({ long double        _; _; }), \
    const long double:        ({ long double        _; _; }), \
    default: x \
  ))

// POW2_CEIL rounds up to power-of-two (from go runtime/stack)
#define POW2_CEIL(v) ({ \
  UNCONST_TYPEOF(v) x = v;  \
  x -= 1;               \
  x = x | (x >> 1);     \
  x = x | (x >> 2);     \
  x = x | (x >> 4);     \
  x = x | (x >> 8);     \
  x = x | (x >> 16);    \
  x + 1; })
