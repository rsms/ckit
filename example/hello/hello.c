#include <rbase/rbase.h>
#include "person.h"

int main(int argc, const char** argv) {
  dlog("argv[0] = %s", argv[0]);

  if (argc > 1 && strcmp(argv[1], "-h") == 0) {
    Str progname = path_base(argv[0]);
    fprintf(stderr, "usage: %s name number\n", progname);
    return 0;
  }

  Person p = {
    .name   = str_cpycstr(argc > 1 ? argv[1] : ""),
    .number = argc > 2 ? atoi(argv[2]) : -1,
  };

  printf("person's name:   \"%s\" (%u bytes)\n", p.name, str_len(p.name));
  printf("person's number: %d\n", p.number);
  return 0;
}
