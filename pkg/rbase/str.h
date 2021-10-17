#pragma once
ASSUME_NONNULL_BEGIN

typedef char* Str;
typedef const char* ConstStr;

// creating new strings
Str        str_new(u32 cap);
void       str_free(Str);
Str        str_fmt(const char* fmt, ...) ATTR_FORMAT(printf, 1, 2);
Str        str_cpy(const char* p, u32 len);
static Str str_cpycstr(const char* cstr);

// properties of a string
static u32  str_len(ConstStr s);
static u32  str_cap(ConstStr s); // does not include terminating \0
static u32  str_avail(ConstStr s);
static Str  str_setlen(Str s, u32 len); // returns s as a convenience
static Str  str_trunc(Str s); // == str_setlen(s, 0)
static int  str_cmp(ConstStr a, ConstStr b); // == memcmp
static bool str_eq(ConstStr a, ConstStr b);

// appending to a string
Str        str_append(Str s, const char* p, u32 len);
static Str str_appendstr(Str s, ConstStr suffix);
static Str str_appendcstr(Str s, const char* cstr);
Str        str_appendc(Str s, char c);
Str        str_appendfmt(Str s, const char* fmt, ...) ATTR_FORMAT(printf, 2, 3);
Str        str_appendfmtv(Str s, const char* fmt, va_list);
Str        str_appendfill(Str s, u32 n, char v); // like msmset(str_reserve)
Str        str_appendu64(Str s, u64 n, u32 base);

// str_appendrepr appends a human-readable representation of data to dst as C-format ASCII
// string literals, with "special" bytes escaped (e.g. \n, \xFE, etc.)
Str str_appendrepr(Str s, const char* data, u32 len);

// str_appendhex appends a hexadecimal representation of data to s, 2 bytes per input byte.
// e.g. ("hello\n",6) => "68656C6C6F0A"
// e.g. ("\0",6)      => "00"
Str str_appendhex(Str s, const u8* data, u32 len);

// str_reserve reserves len bytes:
// 1. grows string if needed, possibly reallocating it
// 3. increments str_len(*sp) by len
// 2. returns a pointer to the _beginning_ of allocated space
char* str_reserve(Str* sp, u32 len);

// str_makeroom ensures that there's at least addlen space available at s
Str str_makeroom(Str s, u32 addlen); // ensures that str_avail()>=addlen

// StrSlice is a temporary pointer into a string
typedef struct StrSlice {
  const char* p;
  size_t      len;
} StrSlice;

// str_split iterates over p of length len, yielding each part separated by delim.
// The function maintains state in sl which is also used for the result (each part.)
// Returns NULL when the end is reached.
//
// Example:
//   StrSlice slice = {};
//   while (str_splitcstr(&slice, '/', "/hello/foo/bar"))
//     printf("\"%.*s\" ", (int)slice.len, slice.p);
// Output: "" "hello" "foo" "bar"
//
static const char* nullable str_split(StrSlice* sl, char delim, Str s);
const char*        nullable str_splitn(StrSlice* sl, char delim, const char* p, size_t len);
static const char* nullable str_splitcstr(StrSlice* sl, char delim, const char* cstr);

// str_hasprefix returns true if s begins with prefix
static bool str_hasprefix(Str s, Str prefix);
bool        str_hasprefixn(Str s, const char* prefix, u32 len);
static bool str_hasprefixcstr(Str s, const char* prefix);

// how string memory is managed
#ifndef R_STR_MEMALLOC
  #define R_STR_MEMALLOC(size)        malloc((size))
  #define R_STR_MEMREALLOC(ptr, size) realloc((ptr), (size))
  #define R_STR_MEMFREE(ptr)          free((ptr))
#endif

// -----------------------------------------------------------------------------------------------
// implementation

struct __attribute__ ((__packed__)) StrHeader {
  u32  len;
  u32  cap; // useful capacity (does not include the sentinel byte)
  char p[];
};

#define STR_HEADER(s) ((struct StrHeader*)( ((char*)s) - sizeof(struct StrHeader) ))

inline static u32 str_len(ConstStr s) { return STR_HEADER(s)->len; }
inline static u32 str_cap(ConstStr s) { return STR_HEADER(s)->cap; }
inline static u32 str_avail(ConstStr s) { return str_cap(s) - str_len(s); }
inline static Str str_setlen(Str s, u32 len) {
  assert(len <= str_cap(s));
  STR_HEADER(s)->len = len;
  s[len] = 0;
  return s;
}
inline static Str str_trunc(Str s) { return str_setlen(s, 0); }

inline static int str_cmp(ConstStr a, ConstStr b) {
  return memcmp(a, b, MIN(str_len(a), str_len(b)));
}

inline static bool str_eq(ConstStr a, ConstStr b) {
  const size_t az = str_len(a);
  const size_t bz = str_len(b);
  return az == bz && memcmp(a, b, az) == 0;
}

inline static Str str_cpycstr(const char* cstr) {
  return str_cpy(cstr, strlen(cstr));
}

inline static Str str_appendstr(Str s, ConstStr suffix) {
  return str_append(s, suffix, str_len(suffix));
}
inline static Str str_appendcstr(Str s, const char* cstr) {
  return str_append(s, cstr, strlen(cstr));
}

inline static const char* str_split(StrSlice* sl, char delim, Str s) {
  return str_splitn(sl, delim, s, str_len(s));
}
inline static const char* str_splitcstr(StrSlice* sl, char delim, const char* cstr) {
  return str_splitn(sl, delim, cstr, strlen(cstr));
}

inline static bool str_hasprefix(Str s, Str prefix) {
  return str_hasprefixn(s, prefix, str_len(prefix));
}
inline static bool str_hasprefixcstr(Str s, const char* prefix) {
  return str_hasprefixn(s, prefix, strlen(prefix));
}


ASSUME_NONNULL_END
