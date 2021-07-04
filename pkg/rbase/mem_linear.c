#include <rbase/rbase.h>

#define ALIGN_ALLOC(size) align2((size), sizeof(void*))

#ifdef DEBUG
  // ckit -debug
  #define DEBUG_MZERO(ptr, size) memset((ptr), 0, (size))
#else
  // ckit -safe | -fast
  #define DEBUG_MZERO(ptr, size) do{}while(0)
#endif

#if defined(DEBUG) || !defined(NDEBUG)
  // ckit -debug | -safe
  #define REPORT_ERROR(fmt, ...) panic(fmt, ##__VA_ARGS__)
#else
  // ckit -fast
  #define REPORT_ERROR(fmt, ...) do{}while(0)
#endif

// AllocHeader is the header of an allocation (for realloc support)
typedef struct AllocHeader {
  size_t size; // number of bytes in data (does not include sizeof(AllocHeader))
  u8     data[];
} AllocHeader;

// Block is the layout of a memory block header
typedef struct Block Block;
typedef struct Block {
  Block* next; // next block in linked list
  size_t size; // number of bytes in total (does NOT include sizeof(Block))
  size_t len;  // number of bytes in use (does NOT include sizeof(Block))
  u8     data[];
} Block;

typedef struct LinearAllocator {
  MemAllocator    a;
  Block*          b;        // allocations block list
  size_t          pagesize; // cached value of mem_pagesize()
} LinearAllocator;

static Block* nullable mla_grow(LinearAllocator* a, size_t size) {
  // account for the space needed by the block header
  size += sizeof(Block);
  // map an extra page, in anticipation of another allocation request
  size += a->pagesize;
  // round up to nearest page
  size_t rem = size % a->pagesize;
  if (rem != 0)
    size += a->pagesize - rem;
  Block* b = (Block*)mem_pagealloc(size / a->pagesize, MemPageDefault);
  if (!b)
    return NULL;
  b->next = a->b;
  b->size = size - sizeof(Block);
  b->len = 0;
  a->b = b;
  return a->b;
}

static void* mla_alloc(Mem m, size_t size) {
  LinearAllocator* a = (LinearAllocator*)m;
  size_t allocsize = ALIGN_ALLOC(size + sizeof(AllocHeader));
  size_t avail = a->b->size - a->b->len;
  //dlog("mla_alloc: reqsize %zu, allocsize %zu, avail %zu", size, allocsize, avail);
  Block* b = a->b;
  if (R_UNLIKELY(avail < allocsize)) {
    b = mla_grow(a, allocsize);
    if (b == NULL)
      return NULL;
  }
  AllocHeader* ah = (AllocHeader*)&b->data[b->len];
  // dlog("alloc: block start - end %p - %p", b, b + b->len);
  // dlog("alloc: block.data[b->len]     %p", &b->data[0]);

  // assertop(allocsize - sizeof(AllocHeader), <=, 0xFFFFFFFF); // must not overflow u32
  // ah->size = (u32)(allocsize - sizeof(AllocHeader));
  ah->size = allocsize - sizeof(AllocHeader);

  b->len += allocsize;
  return &ah->data[0];
}

// find_block looks up the block that ptr belongs to.
// Returns NULL if ptr does not belong in any block.
// O(n) complexity where n is the number of blocks in the allocator.
static Block* nullable find_block(LinearAllocator* a, void* ptr) {
  Block* b = a->b;
  const uintptr_t addr = ((uintptr_t)ptr) - sizeof(AllocHeader);
  while (1) {
    const uintptr_t blockStartAddr = (uintptr_t)&b->data[0];
    const uintptr_t blockEndAddr = (uintptr_t)&b->data[b->size];
    if (addr >= blockStartAddr && addr < blockEndAddr)
      return b;
    if (b->next == NULL) {
      REPORT_ERROR("%p does not belong to this allocator", ptr);
      return NULL;
    }
    b = b->next;
  }
}

// is_most_recent_alloc returns true if ah it the most recent allocation made.
// I.e. if it's at the top of the allocator. b must be the block that ah belongs to.
inline static bool is_most_recent_alloc(LinearAllocator* a, Block* b, AllocHeader* ah) {
  if (b == a->b) {
    uintptr_t endAddr = (uintptr_t)&ah->data[0] + ah->size;
    uintptr_t blockLastAddr = (uintptr_t)&b->data[b->len];
    if (endAddr == blockLastAddr) {
      return true;
    }
  }
  return false;
}

static void mla_free(Mem m, void* ptr) {
  LinearAllocator* a = (LinearAllocator*)m;
  Block* b = find_block(a, ptr);
  if (b) {
    AllocHeader* ah = (AllocHeader*)(ptr - sizeof(AllocHeader));
    if (is_most_recent_alloc(a, b, ah)) {
      b->len -= sizeof(AllocHeader) + ah->size;
      DEBUG_MZERO(ah, sizeof(AllocHeader) + ah->size);
    }
  }
}

static void* mla_realloc(Mem m, void* ptr, size_t newsize) {
  LinearAllocator* a = (LinearAllocator*)m;

  // locate its block
  Block* b = find_block(a, ptr);
  if (!b) {
    return NULL;
  }

  // access allocation header to get size
  AllocHeader* ah = (AllocHeader*)(ptr - sizeof(AllocHeader));
  if (R_UNLIKELY(newsize == ah->size))
    return ptr;

  // if the allocation is at the top (was the most recent allocation), then we may be able
  // to simply grow it.
  if (is_most_recent_alloc(a, b, ah)) {
    if (newsize < ah->size) {
      // fast path: shrink the top
      b->len -= ah->size - newsize;
      ah->size = newsize;
      return ptr;
    }
    size_t avail = b->size - b->len; // available space in block b
    if (avail - ah->size >= newsize) {
      // fast path: grow in available space
      b->len += newsize - ah->size;
      ah->size = newsize;
      return ptr;
    }
  }

  if (R_UNLIKELY(newsize < ah->size)) {
    // shrink an allocation which is not at the top (noop)
    return ptr;
  }

  // create a new allocation and copy the data
  void* ptr2 = mla_alloc(m, newsize);
  memcpy(ptr2, ptr, ah->size);
  DEBUG_MZERO(ah, sizeof(AllocHeader) + ah->size);
  return ptr2;
}

Mem nullable MemLinearAlloc(size_t npages_init) {
  // allocate a first page to store the allocator in as well as initial memory
  assert(npages_init > 0);
  Block* b = (Block*)mem_pagealloc(npages_init, MemPageDefault);
  if (!b)
    return NULL;
  size_t pagesize = mem_pagesize();
  b->next = NULL;
  b->size = (npages_init * pagesize) - sizeof(Block);
  b->len = sizeof(LinearAllocator);
  // place the allocator in the first block
  LinearAllocator* a = (LinearAllocator*)&b->data[0];
  a->a.alloc = mla_alloc;
  a->a.realloc = mla_realloc;
  a->a.free = mla_free;
  a->b = b;
  a->pagesize = pagesize;
  return (Mem)a;
}

// returns b->next
static Block* nullable mla_free_block(LinearAllocator* a, Block* b) {
  Block* next = b->next;
  if (!mem_pagefree(b, (b->size + sizeof(Block)) / a->pagesize))
    REPORT_ERROR("mem_pagefree failed (errno %d)", errno);
  return next;
}

void MemLinearReset(Mem m) {
  LinearAllocator* a = (LinearAllocator*)m;
  // Keep the root block, which houses the LinearAllocator data.
  // TODO: consider keeping the largest block and if that is different from the root block,
  // memcpy the LinearAllocator data.
  Block* b = a->b;
  while (b->next) {
    b = mla_free_block(a, b);
  }
  a->b = b;
  b->len = sizeof(LinearAllocator);
}

void MemLinearFree(Mem m) {
  LinearAllocator* a = (LinearAllocator*)m;
  Block* b = a->b;
  while (b) {
    b = mla_free_block(a, b);
  }
}


// ——————————————————————————————————————————————————————————————————————————————

R_TEST(mem_linear) {

  { // alloc
    auto m = MemLinearAlloc(1);
    //dlog("m %p", m);
    size_t reqsize = 9;
    void* p1 = memalloc(m, reqsize);
    assertnotnull(p1);
    void* p2 = memalloc(m, reqsize);
    assertnotnull(p2);
    // check address distance of the two adjacent allocations
    ptrdiff_t p1_to_p2 = (ptrdiff_t)(p2 - p1);
    asserteq(p1_to_p2, ALIGN_ALLOC(reqsize + sizeof(AllocHeader)));
    MemLinearFree(m);
  }

  { // requesting a page should push it to grow()
    auto m = MemLinearAlloc(1);
    //dlog("m %p", m);
    void* p1 = memalloc(m, 8);
    void* p2 = memalloc(m, mem_pagesize() * 2);
    assertnotnull(p1);
    assertnotnull(p2);
    auto b1 = find_block((LinearAllocator*)m, p1);
    auto b2 = find_block((LinearAllocator*)m, p2);
    assertne(b1, b2); // p1 and p2 should belong to different blocks
    MemLinearFree(m);
  }

  { // free of most recent allocation
    auto m = MemLinearAlloc(1);
    //dlog("m %p", m);
    void* p1 = memalloc(m, 8);
    assertnotnull(p1);
    memfree(m, p1); // should recycle p1, like a "stack pop" ...
    void* p2 = memalloc(m, 12); // ... and a "stack push"
    assertnotnull(p2);
    asserteq(p1, p2); // p2 should be the same address as p1
    MemLinearFree(m);
  }

  { // realloc, ideal case: extend existing segment
    auto m = MemLinearAlloc(1);
    //dlog("m %p", m);
    void* p1 = memalloc(m, 9);
    assertnotnull(p1);
    void* p1b = memrealloc(m, p1, 20);
    asserteq(p1, p1b); // should not have moved
    MemLinearFree(m);
  }

  { // realloc, common case: move allocation
    auto m = MemLinearAlloc(1);
    char* p1 = (char*)memalloc(m, 9);
    memcpy(p1, "hello", 5);
    assertnotnull(p1);
    UNUSED void* _ = memalloc(m, 1);
    char* p1b = (char*)memrealloc(m, p1, 20);
    assert(p1 != p1b); // should have moved
    asserteq(memcmp(p1b, "hello", 5), 0); // contents should not have changed
    MemLinearFree(m);
  }

  { // realloc, shrink
    auto m = MemLinearAlloc(1);
    void* p1 = memalloc(m, 9);
    assertnotnull(p1);
    void* p1b = memrealloc(m, p1, 4);
    asserteq(p1, p1b); // should not have moved
    MemLinearFree(m);
  }

  { // MemLinearReset
    auto m = MemLinearAlloc(1);
    size_t reqsize = 9;

    void* p1a = memalloc(m, reqsize);
    void* p2a = memalloc(m, reqsize);
    void* block1_a = ((LinearAllocator*)m)->b;
    // make sure to cause a new block to be allocated for p3
    assertnotnull(memalloc(m, mem_pagesize() * 2));
    assertnotnull(p1a);
    assertnotnull(p2a);

    MemLinearReset(m);

    void* p1b = memalloc(m, reqsize);
    void* p2b = memalloc(m, reqsize);
    void* block1_b = ((LinearAllocator*)m)->b;

    asserteq(p1b, p1a);
    asserteq(p2b, p2a);
    asserteq(block1_b, block1_a); // reset should have kept the root block

    MemLinearFree(m);
  }
}
