#include "rbase.h"

// ---------------------------------------------------------------------------------------------
#if defined(__MACH__) && defined(__APPLE__)
#include <mach-o/dyld.h>

// [from man 3 dyld]
// _NSGetExecutablePath() copies the path of the main executable into the buffer buf.
// The bufsize parameter should initially be the size of the buffer.
// This function returns 0 if the path was successfully copied, and *bufsize is left unchanged.
// It returns -1 if the buffer is not large enough, and *bufsize is set to the size required.
// Note that _NSGetExecutablePath() will return "a path" to the executable not a "real path"
// to the executable.
// That is, the path may be a symbolic link and not the real file. With deep directories the
// total bufsize needed could be more than MAXPATHLEN.

const char* os_exepath() {
  static char pathst[256];
  static char* path = pathst;
  static r_sync_once_flag onceflag;

  r_sync_once(&onceflag, {
    u32 len = sizeof(pathst);
    while (1) {
      if (_NSGetExecutablePath(path, &len) == 0) {
        path = realpath(path, NULL);
        break;
      }
      if (len >= PATH_MAX) {
        // Don't keep growing the buffer at this point.
        // _NSGetExecutablePath may be failing for other reasons.
        // Fall back on libc getprogname
        realpath(getprogname(), path);
        break;
      }
      // not enough space in path
      len *= 2;
      if (path == pathst) {
        path = memalloc(MemLibC(), len);
      } else {
        path = memrealloc(MemLibC(), path, len);
      }
    }
  });

  return path;
}

// ---------------------------------------------------------------------------------------------
#else
  #error "os_exepath not implemented for this OS"
  // Linux: readlink /proc/self/exe
  // FreeBSD: sysctl CTL_KERN KERN_PROC KERN_PROC_PATHNAME -1
  // FreeBSD if it has procfs: readlink /proc/curproc/file (FreeBSD doesn't have procfs by default)
  // NetBSD: readlink /proc/curproc/exe
  // DragonFly BSD: readlink /proc/curproc/file
  // Solaris: getexecname()
  // Windows: GetModuleFileName() with hModule = NULL
  // From https://stackoverflow.com/a/1024937
#endif
