#pragma once
// R_MEM_DEBUG_ALLOCATIONS: define to dlog all calls to alloc, realloc and free.
//#define R_MEM_DEBUG_ALLOCATIONS
#if __has_attribute(malloc)
  #define R_ATTR_MALLOC __attribute__((malloc))
#else
  #define R_ATTR_MALLOC
#endif
#if __has_attribute(alloc_size)
  #define R_ATTR_ALLOC_SIZE(whicharg) __attribute__((alloc_size(whicharg)))
#else
  #define R_ATTR_ALLOC_SIZE(whicharg)
#endif
ASSUME_NONNULL_BEGIN


// Mem is the handle of a memory allocator
typedef const struct MemAllocator* Mem;

// MemLibC returns a shared thread-safe heap allocator using libc (malloc, free et al)
static Mem MemLibC();

// MemInvalid returns a memory allocator which panics on allocation in debug and safe builds,
// while always returning NULL in all build modes.
// Useful when you expect a thing not to make allocations.
static Mem MemInvalid();


// mem2alloc allocates memory of at least size. Returned memory is zeroed.
static void* nullable memalloc(Mem m, size_t size)
  R_ATTR_MALLOC R_ATTR_ALLOC_SIZE(2) WARN_UNUSED_RESULT;

// memrealloc resizes memory at ptr. If ptr is null, the behavior matches memalloc.
static void* nullable memrealloc(Mem m, void* nullable ptr, size_t newsize)
  R_ATTR_ALLOC_SIZE(3) WARN_UNUSED_RESULT;

// memfree frees memory allocated with memalloc
static void memfree(Mem m, void* ptr);

// memalloct is a convenience for: (MyStructType*)memalloc(m, sizeof(MyStructType))
#define memalloct(mem, TYPE) ((TYPE*)memalloc((mem),sizeof(TYPE)))

// mem_pagesize returns the system's memory page size, which is usually 4096 bytes.
// This function returns a cached value from memory, read from the OS at init.
size_t mem_pagesize();

// memdup makes a copy of src
static void* nullable memdup(Mem m, const void* src, size_t len);

// memdup2 makes a copy of src with optional extraspace at the end.
void* nullable memdup2(Mem m, const void* src, size_t len, size_t extraspace);

// memstrdup is like strdup but uses m
char* nullable memstrdup(Mem m, const char* pch);

// ---------------------------------

// MemAllocator is the implementation interface for an allocator.
typedef struct MemAllocator {
  // alloc should allocate at least size contiguous memory and return the address.
  // If it's unable to do so it should return NULL.
  void* nullable (*alloc)(Mem m, size_t size);

  // realloc is called with the address of a previous allocation of the same allocator m.
  // It should either extend the contiguous memory segment at ptr to be at least newsize
  // long in total, or allocate a new contiguous memory of at least newsize.
  // If it's unable to fulfill the request it should return NULL.
  // Note that ptr is never NULL; calls to memrealloc with a NULL ptr are routed to alloc.
  void* nullable (*realloc)(Mem m, void* ptr, size_t newsize);

  // free is called with the address of a previous allocation of the same allocator m.
  // The allocator now owns the memory at ptr and may recycle it or release it to the system.
  void (*free)(Mem m, void* ptr);
} MemAllocator;

// ---------------------------------

// MemLinearAlloc creates a new linear allocator.
// This is a good choice when you need to burn through a lot of temporary allocations
// and then free them all. memfree is a noop except for the most recent allocation which simply
// rewindws the allocator.
// npages_init is the number of memory pages to map (but not commit) up front.
// This allocator is NOT thread safe. Use MemSyncWrapper if MT access is needed.
Mem nullable MemLinearAlloc(size_t npages_init);

// MemLinearReset resets m, which must have been created with MemLinearAlloc, as if
// nothing is allocated. Useful for resuing already-allocated memory.
void MemLinearReset(Mem m);

// MemLinearFree frees all memory allocated in m, including m itself.
void MemLinearFree(Mem m);

// ---------------------------------

// MemSyncWrapper creates an allocator which wraps another allocator and
// uses a mtx_t to ensure mutually exclusive access to the alloc, realloc
// and free functions of the provided allocator m.
Mem MemSyncWrapper(Mem inner);
Mem MemSyncWrapperFree(Mem m); // returns the "unwrapped" inner allocator

// ---------------------------------

// MemArenaShim is a memory arena which delegates memory management to an underlying allocator.
// This is useful when you want to make a lot of allocations and then free them all at once,
// removing the need for fine-grained memfree calls.
typedef struct MemArenaShim {
  MemAllocator ma;
  Mem          underlying;
  struct MemArenaShimEntry* nullable allocations;
} MemArenaShim;

// MemArenaShimInit initializes a new arena
Mem MemArenaShimInit(MemArenaShim* a, Mem underlying);

// MemArenaShimFree frees all memory allocated in the arena.
// The arena and all memory allocated with it is invalid after this call.
void MemArenaShimFree(MemArenaShim* a);

// ---------------------------------

// MemPageFlags control the behavior of mem_pagealloc
typedef enum {
  MemPageDefault   = 0,
  MemPageGuardHigh = 1 << 0, // mprotect the last page
  MemPageGuardLow  = 1 << 1, // mprotect the first page (e.g. for stack memory)
} MemPageFlags;

// mem_pagealloc allocates npages from the OS. Returns NULL on error.
void* nullable mem_pagealloc(size_t npages, MemPageFlags);

// mem_pagefree frees pages allocated with mem_pagealloc. Returns false and sets errno on error.
bool mem_pagefree(void* ptr, size_t npages);

// ---------------------------------

/*
Note on runtime overhead:
Recent versions of GCC or Clang will optimize calls to memalloc(MemLibC(), z) into
direct calls to the clib functions and completely eliminate the code declared in this file.
Example code generation:
  int main() {
    void* p = memalloc(MemLibC(), 12);
    return (int)p;
  }
  —————————————————————————————————————————————————————————
  x86_64 clang-12 -O2:  | arm64 clang-11 -O2:
    main:               |   main:
      mov     edi, 1    |     stp     x29, x30, [sp, #-16]!
      mov     esi, 12   |     mov     x29, sp
      jmp     calloc    |     mov     w0, #1
                        |     mov     w1, #12
                        |     bl      calloc
                        |     ldp     x29, x30, [sp], #16
                        |     ret
  —————————————————————————————————————————————————————————
  x86_64 gcc-11 -O2:    | arm64 gcc-11 -O2:
    main:               |   main:
      sub     rsp, 8    |     stp     x29, x30, [sp, -16]!
      mov     esi, 12   |     mov     x1, 12
      mov     edi, 1    |     mov     x0, 1
      call    calloc    |     mov     x29, sp
      add     rsp, 8    |     bl      calloc
      ret               |     ldp     x29, x30, [sp], 16
                        |     ret
  —————————————————————————————————————————————————————————
  https://godbolt.org/z/MK757acnK
*/


// -------------------------------------------------------------------------------------
// implementation

static void* nullable _mem_invalid_alloc(Mem _, size_t size) {
  assert(!"allocation requested from invalid allocator");
  return NULL;
}
static void* nullable _mem_invalid_realloc(Mem _, void* ptr, size_t newsize) {
  assert(!"allocation requested from invalid allocator");
  return NULL;
}
static void _mem_invalid_free(Mem _, void* ptr) {
  assert(!"free called on invalid allocator");
}
__attribute__((used))
static const MemAllocator _mem_invalid = {
  .alloc   = _mem_invalid_alloc,
  .realloc = _mem_invalid_realloc,
  .free    = _mem_invalid_free,
};
inline static Mem MemInvalid() {
  return &_mem_invalid;
}


static void* nullable _mem_libc_alloc(Mem _, size_t size) {
  return calloc(1, size);
}
static void* nullable _mem_libc_realloc(Mem _, void* ptr, size_t newsize) {
  return realloc(ptr, newsize);
}
static void _mem_libc_free(Mem _, void* ptr) {
  free(ptr);
}
__attribute__((used))
static const MemAllocator _mem_libc = {
  .alloc   = _mem_libc_alloc,
  .realloc = _mem_libc_realloc,
  .free    = _mem_libc_free,
};
inline static Mem MemLibC() {
  return &_mem_libc;
}


inline static void* memalloc(Mem m, size_t size) {
  assertnotnull_debug(m);
  void* p = m->alloc(m, size);
  #ifdef R_MEM_DEBUG_ALLOCATIONS
  dlog("memalloc %p-%p (%zu)", p, p + size, size);
  #endif
  return p;
}

inline static void* memrealloc(Mem m, void* nullable ptr, size_t newsize) {
  assertnotnull_debug(m);
  void* p = ptr ? m->realloc(m, ptr, newsize) : m->alloc(m, newsize);
  #ifdef R_MEM_DEBUG_ALLOCATIONS
  dlog("realloc %p -> %p-%p (%zu)", ptr, p, p + newsize, newsize);
  #endif
  return p;
}

inline static void memfree(Mem m, void* _Nonnull ptr) {
  assertnotnull_debug(m);
  assertnotnull_debug(ptr);
  #ifdef R_MEM_DEBUG_ALLOCATIONS
  dlog("memfree %p", ptr);
  #endif
  m->free(m, ptr);
}

inline static void* memdup(Mem m, const void* src, size_t len) {
  return memdup2(m, src, len, 0);
}

ASSUME_NONNULL_END
