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

// ------------------------------------------------------------------------------------
// MemSyncWrapper

typedef struct MSyncWrapper {
  MemAllocator ma;
  mtx_t        mu;
  Mem          m;
} MSyncWrapper;

static void* nullable _mem_syncw_alloc(Mem m, size_t size) {
  MSyncWrapper* w = (MSyncWrapper*)m;
  mtx_lock(&w->mu);
  void* p = m->alloc(w->m, size);
  mtx_unlock(&w->mu);
  return p;
}

static void* nullable _mem_syncw_realloc(Mem m, void* ptr, size_t newsize) {
  MSyncWrapper* w = (MSyncWrapper*)m;
  mtx_lock(&w->mu);
  void* p = m->realloc(w->m, ptr, newsize);
  mtx_unlock(&w->mu);
  return p;
}

static void _mem_syncw_free(Mem m, void* ptr) {
  MSyncWrapper* w = (MSyncWrapper*)m;
  mtx_lock(&w->mu);
  m->free(w->m, ptr);
  mtx_unlock(&w->mu);
}

Mem MemSyncWrapper(Mem inner) {
  auto w = memalloct(inner, MSyncWrapper);
  w->ma.alloc   = _mem_syncw_alloc;
  w->ma.realloc = _mem_syncw_realloc;
  w->ma.free    = _mem_syncw_free;
  mtx_init(&w->mu, mtx_plain);
  w->m = inner;
  return (Mem)w;
}

Mem MemSyncWrapperFree(Mem m) {
  auto w = (MSyncWrapper*)m;
  Mem inner = w->m;
  memfree(inner, w);
  return inner;
}
