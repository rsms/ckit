#include "rbase.h"


Str os_getcwd_str() {
  static size_t pathmax;
  static r_sync_once_flag onceflag = {0};
  r_sync_once(&onceflag, {
    pathmax = (size_t)pathconf(".", _PC_PATH_MAX);
  });
  Str s = str_new(pathmax);
  if (s && getcwd(s, str_cap(s)))
    str_setlen(s, strlen(s));
  return s;
}
