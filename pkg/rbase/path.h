#pragma once
ASSUME_NONNULL_BEGIN

#if WIN32
  #define PATH_SEPARATOR     '\\'
  #define PATH_SEPARATOR_STR "\\"
  #define PATH_DELIMITER     ';'
  #define PATH_DELIMITER_STR ";"
#else
  #define PATH_SEPARATOR     '/'
  #define PATH_SEPARATOR_STR "/"
  #define PATH_DELIMITER     ':'
  #define PATH_DELIMITER_STR ":"
#endif

// path_isabs returns true if path is an absolute path
bool path_isabs(const char* path);

// path_join returns a new string of path1 + PATH_SEPARATOR + path2
Str path_join(const char* path1, const char* path2);

// path_dir returns the directory part of path (i.e. "foo/bar/baz" => "foo/bar")
Str path_dir(const char* path);
char* path_dir_mut(char* path);

// path_base returns the last path element. E.g. "foo/bar/baz.x" => "baz.x"
// Trailing slashes are removed before extracting the last element.
// If the path is empty, returns ".".
// If the path consists entirely of slashes, returns "/".
Str path_base(const char* path);
Str path_base_append(Str, const char* path);

// path_ext returns the file name extension of path. E.g. "foo/bar.baz" => ".baz"
// The extension is the suffix beginning at the final dot in the final slash-separated
// element of path; it is empty if there is no dot. If the final dot is the first character
// of the path's base, an empty string is returned. I.e. "foo/.dotfile" => ""
const char* path_ext(const char* path);

// path_cwdrel returns path relative to the current working directory, or path verbatim if
// path is outside the working directory.
const char* path_cwdrel(const char* path);


ASSUME_NONNULL_END
