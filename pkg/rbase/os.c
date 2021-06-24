#include "rbase.h"
#include <pwd.h> // getpwuid

ASSUME_NONNULL_BEGIN

const char* os_user_home_dir() {
  struct passwd* pw = getpwuid(getuid());
  if (pw && pw->pw_dir)
    return pw->pw_dir;
  auto home = getenv("HOME");
  if (home)
    return home;
  return "";
}


ASSUME_NONNULL_END
