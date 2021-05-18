#include <stdarg.h>
#include "rbase.h"

void* memdup2(Mem mem, const void* src, size_t len, size_t extraspace) {
  void* dst = mem->alloc(mem, len + extraspace);
  memcpy(dst, src, len);
  return dst;
}

char* memstrdup(Mem mem, const char* pch) {
  size_t z = strlen(pch);
  char* s = (char*)memdup2(mem, pch, z, 1);
  s[z] = 0;
  return s;
}
