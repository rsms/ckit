#include <stdarg.h>
#include "rbase.h"

void* memdup2(Mem mem, const void* src, size_t len, size_t extraspace) {
  assertnotnull_debug(mem);
  assertnotnull(src);
  void* dst = mem->alloc(mem, len + extraspace);
  memcpy(dst, src, len);
  return dst;
}

char* memstrdup(Mem mem, const char* pch) {
  assertnotnull_debug(mem);
  assertnotnull(pch);
  size_t z = strlen(pch);
  char* s = (char*)memdup2(mem, pch, z, 1);
  s[z] = 0;
  return s;
}
