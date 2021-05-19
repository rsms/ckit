typedef struct MemArenaShimEntry {
  struct MemArenaShimEntry* next;
  u8 data[0];
} MemArenaShimEntry;

// MemArenaShimEntry* -> void*
#define ENT_TO_PTR(e) (&(e)->data[0])

// void* -> MemArenaShimEntry*
#define PTR_TO_ENT(ptr) ( (MemArenaShimEntry*)(((u8*)(ptr)) - sizeof(MemArenaShimEntry)) )

static void* MemArenaShim_alloc(Mem m, size_t size) {
  MemArenaShim* a = (MemArenaShim*)m;
  auto e = (MemArenaShimEntry*)memalloc(a->underlying, sizeof(MemArenaShimEntry) + size);
  e->next = a->allocations;
  a->allocations = e->next;
  return ENT_TO_PTR(e);
}

static void* MemArenaShim_realloc(Mem m, void* ptr, size_t newsize) {
  MemArenaShim* a = (MemArenaShim*)m;
  assertnotnull(ptr);
  auto olde = PTR_TO_ENT(ptr);
  auto newe = (MemArenaShimEntry*)memrealloc(
    a->underlying, olde, sizeof(MemArenaShimEntry) + newsize);
  void* ptr2 = ENT_TO_PTR(newe);
  if (ptr != ptr2) {
    // allocation moved
    MemArenaShimEntry* e = a->allocations;
    MemArenaShimEntry* preve = NULL;
    while (e) {
      if (ENT_TO_PTR(e) == ptr) {
        if (preve) {
          // middle or tail
          preve->next = e->next;
        } else {
          // head
          a->allocations = e->next;
        }
        memfree(a->underlying, e);
        break;
      }
      preve = e;
      e = e->next;
    }
  }
  return ptr2;
}

static void MemArenaShim_free(Mem _, void* ptr) {
  // no-op
}

Mem MemArenaShimInit(MemArenaShim* a, Mem underlying) {
  a->underlying = underlying;
  a->allocations = NULL;
  a->ma.alloc   = MemArenaShim_alloc;
  a->ma.realloc = MemArenaShim_realloc;
  a->ma.free    = MemArenaShim_free;
  return &a->ma;
}

void MemArenaShimFree(MemArenaShim* a) {
  MemArenaShimEntry* e = a->allocations;
  while (e) {
    memfree(a->underlying, e);
    e = e->next;
  }
}
