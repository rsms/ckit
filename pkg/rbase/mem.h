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

// MemGeneric returns a shared generic heap allocator which is thread safe.
static Mem MemGeneric();


// mem2alloc allocates memory of at least size. Returned memory is zeroed.
static void* nullable memalloc(Mem m, size_t size)
  R_ATTR_MALLOC R_ATTR_ALLOC_SIZE(2) WARN_UNUSED_RESULT;

// memrealloc resizes memory at ptr
static void* nullable memrealloc(Mem m, void* ptr, size_t newsize)
  R_ATTR_ALLOC_SIZE(3) WARN_UNUSED_RESULT;

// memfree frees memory allocated with memalloc
static void memfree(Mem m, void* ptr);

// memalloct is a convenience for: (MyStructType*)memalloc(m, sizeof(MyStructType))
#define memalloct(mem, TYPE) ((TYPE*)memalloc((mem),sizeof(TYPE)))

// mem_pagesize returns the system's memory page size, which is usually 4096 bytes.
// This function returns a cached value from memory, read from the OS at init.
size_t mem_pagesize();

// memdup makes a copy of src
static void* memdup(Mem m, const void* src, size_t len);

// memdup2 is like memdup but takes an additional arg extraspace for allocating additional
// uninitialized space after len.
void* memdup2(Mem m, const void* src, size_t len, size_t extraspace);

// memstrdup is like strdup but uses m
char* memstrdup(Mem m, const char* nonull pch);


// MemAllocator is the implementation interface for an allocator
typedef struct MemAllocator {
  void* nullable (*alloc)(Mem m, size_t size);
  void* nullable (*realloc)(Mem m, void* ptr, size_t newsize);
  void           (*free)(Mem m, void* ptr);
} MemAllocator;


// -------------------------------------------------------------------------------------
// implementation


static void* _mem_generic_alloc(Mem _, size_t size) {
  return calloc(1, size);
}

static void* _mem_generic_realloc(Mem _, void* ptr, size_t newsize) {
  return realloc(ptr, newsize);
}

static void _mem_generic_free(Mem _, void* ptr) {
  free(ptr);
}

__attribute__((used))
static MemAllocator _mem_generic = {
  .alloc   = _mem_generic_alloc,
  .realloc = _mem_generic_realloc,
  .free    = _mem_generic_free,
};

inline static Mem MemGeneric() {
  return &_mem_generic;
}

inline static void* memalloc(Mem m, size_t size) {
  void* p = m->alloc(m, size);
  #ifdef R_MEM_DEBUG_ALLOCATIONS
  dlog("memalloc %p-%p (%zu)", p, p + size, size);
  #endif
  return p;
}

inline static void* memrealloc(Mem m, void* ptr, size_t newsize) {
  void* p = m->realloc(m, ptr, newsize);
  #ifdef R_MEM_DEBUG_ALLOCATIONS
  dlog("realloc %p -> %p-%p (%zu)", ptr, p, p + newsize, newsize);
  #endif
  return p;
}

inline static void memfree(Mem m, void* ptr) {
  #ifdef R_MEM_DEBUG_ALLOCATIONS
  dlog("memfree %p", ptr);
  #endif
  m->free(m, ptr);
}

inline static void* memdup(Mem mem, const void* src, size_t len) {
  return memdup2(mem, src, len, 0);
}

ASSUME_NONNULL_END
