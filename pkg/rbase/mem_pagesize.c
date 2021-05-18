#include <stdarg.h>
#include "mem.h"

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

size_t mem_pagesize() {
  static size_t v = 0;
  if (v == 0) {
    // atomicity: ok to call multiple times
    v = MEM_GETPAGESIZE;
  }
  return v;
}
