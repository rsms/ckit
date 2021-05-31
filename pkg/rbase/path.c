#include "rbase.h"
#include <libgen.h> // dirname

// path_isabs returns true if filename is an absolute path
bool path_isabs(const char* filename) {
  // TODO windows
  size_t z = strlen(filename);
  return z == 0 || filename[0] == PATH_SEPARATOR;
}

Str path_join(const char* path1, const char* path2) {
  size_t len1 = strlen(path1);
  size_t len2 = strlen(path2);
  auto s = str_new(len1 + len2 + 1);
  s = str_append(s, path1, len1);
  s = str_appendc(s, PATH_SEPARATOR);
  s = str_append(s, path2, len2);
  return s;
}

Str path_dir(const char* filename) {
  auto p = strrchr(filename, PATH_SEPARATOR);
  if (p == NULL)
    return str_cpycstr(".");
  return str_cpy(filename, (u32)(p - filename));
}

char* path_dir_mut(char* filename) {
  return dirname(filename);
}


// impl of Python behavior: ".dotfile" => ""
const char* path_ext(const char* path) {
  ssize_t len = (ssize_t)strlen(path);
  for (ssize_t i = len - 1; i > 0 && path[i] != PATH_SEPARATOR; i--) {
    if (path[i] == '.' && path[i - 1] != PATH_SEPARATOR)
      return path + i;
  }
  return path + len;
}

// // impl of Go behavior: ".dotfile" => ".dotfile"
// const char* path_ext(const char* path) {
//   ssize_t len = (ssize_t)strlen(path);
//   for (ssize_t i = len - 1; i >= 0 && path[i] != PATH_SEPARATOR; i--) {
//     if (path[i] == '.')
//       return path + i;
//   }
//   return path + len;
// }

R_TEST(path_ext) {
  assertcstreq(path_ext("cat.xyz"), ".xyz");
  assertcstreq(path_ext("/foo/bar/cat.xyz"), ".xyz");
  assertcstreq(path_ext("/foo/bar/cat"), "");

  // file which base starts with "."
  // this is what Python and Nodejs does:
  assertcstreq(path_ext(".xyz"), "");
  assertcstreq(path_ext("/foo/bar/.xyz"), "");
  // this is what Go does:
  // assertcstreq(path_ext(".xyz"), ".xyz");
  // assertcstreq(path_ext("/foo/bar/.xyz"), ".xyz");
}


// like strrchr
static ssize_t last_slash(const char* s, size_t len) {
  ssize_t i = ((ssize_t)len) - 1;
  while (i >= 0 && s[i] != PATH_SEPARATOR)
    i--;
  return i;
}

Str path_base_append(Str s, const char* path) {
  size_t len = strlen(path);
  if (len == 0)
    return str_appendc(s, '.');

  // strip trailing slashes
  while (len > 0 && path[len - 1] == PATH_SEPARATOR)
    len--;

  // find last element
  ssize_t i = last_slash(path, len);
  if (i >= 0) {
    i++;
    path += i;
    len -= i;
  }

  // if empty now, it had only slashes
  if (len == 0)
    return str_appendc(s, PATH_SEPARATOR);

  return str_append(s, path, len);
}

Str path_base(const char* path) {
  return path_base_append(str_new(0), path);
}

R_TEST(path_base) {
  Str s = str_new(32);
  #define S(EXPR) ({ str_setlen(s, 0); s = EXPR; s; })

  assertcstreq(S(path_base_append(s, "")),               ".");
  assertcstreq(S(path_base_append(s, ".")),              ".");
  assertcstreq(S(path_base_append(s, "////")),           "/");
  assertcstreq(S(path_base_append(s, "foo/bar")),        "bar");
  assertcstreq(S(path_base_append(s, "///foo/bar////")), "bar");
  assertcstreq(S(path_base_append(s, "/foo/bar")),       "bar");
  assertcstreq(S(path_base_append(s, "foo")),            "foo");

  #undef S
  str_free(s);
}


const char* path_cwdrel(const char* path) {
  if (!path_isabs(path))
    return path;
  Str cwd = os_getcwd_str();
  size_t pathlen = strlen(path);
  size_t cwdlen = str_len(cwd);
  if (cwdlen != 0 && cwdlen < pathlen && memcmp(cwd, path, cwdlen) == 0) {
    // path has prefix cwd
    path = &path[cwdlen + 1]; // e.g. "/foo/bar/baz" => "bar/baz"
  }
  str_free(cwd);
  return path;
}

