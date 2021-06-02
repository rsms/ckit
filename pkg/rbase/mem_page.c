#include <rbase/rbase.h>
// #include <stdarg.h>

// define a "portable" macro MEM_GETPAGESIZE returning a size_t value
#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#  include <tchar.h>
#  define MEM_GETPAGESIZE malloc_getpagesize
#elif defined(malloc_getpagesize)
#  define MEM_GETPAGESIZE malloc_getpagesize
#else
#  include <unistd.h>
#  ifdef _SC_PAGESIZE         /* some SVR4 systems omit an underscore */
#    ifndef _SC_PAGE_SIZE
#      define _SC_PAGE_SIZE _SC_PAGESIZE
#    endif
#  endif
#  ifdef _SC_PAGE_SIZE
#    define MEM_GETPAGESIZE sysconf(_SC_PAGE_SIZE)
#  else
#    if defined(BSD) || defined(DGUX) || defined(HAVE_GETPAGESIZE)
       extern size_t getpagesize();
#      define MEM_GETPAGESIZE getpagesize()
#    else
#      ifndef LACKS_SYS_PARAM_H
#        include <sys/param.h>
#      endif
#      ifdef EXEC_PAGESIZE
#        define MEM_GETPAGESIZE EXEC_PAGESIZE
#      else
#        ifdef NBPG
#          ifndef CLSIZE
#            define MEM_GETPAGESIZE NBPG
#          else
#            define MEM_GETPAGESIZE (NBPG * CLSIZE)
#          endif
#        else
#          ifdef NBPC
#            define MEM_GETPAGESIZE NBPC
#          else
#            ifdef PAGESIZE
#              define MEM_GETPAGESIZE PAGESIZE
#            else /* just guess */
#              define MEM_GETPAGESIZE ((size_t)4096U)
#            endif
#          endif
#        endif
#      endif
#    endif
#  endif
#endif /* _WIN32 */


#ifdef _WIN32
  #error "TODO win32 VirtualAlloc"
#else
  #define HAS_MMAP
  #define HAS_MPROTECT
  #include <sys/types.h>
  #include <sys/mman.h>
  #include <sys/resource.h>
  #if defined(__MACH__) && defined(__APPLE__)
    #include <mach/vm_statistics.h>
    #include <mach/vm_prot.h>
  #endif
  #ifndef MAP_ANON
    #define MAP_ANON MAP_ANONYMOUS
  #endif
#endif


size_t mem_pagesize() {
  static size_t v = 0;
  if (v == 0) {
    // atomicity: ok to call multiple times
    v = MEM_GETPAGESIZE;
  }
  return v;
}


void* nullable mem_pagealloc(size_t npages, MemPageFlags flags) {
  // read system page size (mem_pagesize returns a cached value; no syscall)
  const size_t pagesize = mem_pagesize();
  size_t allocsize = npages * pagesize;
  void* ptr = NULL;

  #ifdef HAS_MMAP
    #if defined(__MACH__) && defined(__APPLE__) && defined(VM_PROT_DEFAULT)
      // vm_map_entry_is_reusable uses VM_PROT_DEFAULT as a condition for page reuse.
      // See http://fxr.watson.org/fxr/source/osfmk/vm/vm_map.c?v=xnu-2050.18.24#L10705
      int mmapprot = VM_PROT_DEFAULT;
    #else
      int mmapprot = PROT_READ | PROT_WRITE;
    #endif

    int mmapflags =
        MAP_PRIVATE
      | MAP_ANON
      #ifdef MAP_NOCACHE
      | MAP_NOCACHE // don't cache pages for this mapping
      #endif
      #ifdef MAP_NORESERVE
      | MAP_NORESERVE // don't reserve needed swap area
      #endif
      ;

    #if defined(__MACH__) && defined(__APPLE__) && defined(VM_FLAGS_PURGABLE)
      int fd = VM_FLAGS_PURGABLE; // Create a purgable VM object for that new VM region.
    #else
      int fd = -1;
    #endif

    ptr = mmap(0, allocsize, mmapprot, mmapflags, fd, 0);
    if (R_UNLIKELY(ptr == MAP_FAILED))
      return NULL;

    // Pretty much all OSes return zeroed anonymous memory pages.
    // Do a little sample in debug mode just to be sure.
    #if defined(DEBUG) && !defined(NDEBUG)
    const u8 zero[128] = {0};
    assert_debug(pagesize >= 128);
    assert_debug(memcmp(ptr, zero, sizeof(zero)) == 0); // or: got random data from mmap!
    #endif

    // if enabled and available, protect the last page from access to cause a crash on
    // out of bounds access.
    #ifdef HAS_MPROTECT
      if (((flags & MemPageGuardHigh) || (flags & MemPageGuardLow)) && npages > 0) {
        void* protPagePtr = ptr;
        if (flags & MemPageGuardHigh) {
          protPagePtr = &((u8*)ptr)[allocsize - pagesize];
        }
        int status = mprotect(protPagePtr, pagesize, PROT_NONE);
        if (R_UNLIKELY(status != 0)) {
          // If mproctect fails, fail hard
          munmap(ptr, allocsize);
          return NULL;
        }
      }
    #endif
  #endif // defined(HAS_MMAP)

  return ptr;
}

bool mem_pagefree(void* ptr, size_t npages) {
  size_t size = mem_pagesize() * npages;
  #ifdef HAS_MMAP
    return munmap(ptr, size) == 0;
  #else
    #error "TODO: no mmap"
  #endif
}
