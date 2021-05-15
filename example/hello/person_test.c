#include <rbase/rbase.h>
#include "person.h"

R_TEST(Person) {
  Person p = {0};
  asserteq(p.number, 0); // C99 (6.7.8.21)
  assertnull(p.name);    // C99 (6.7.8.21)
  p.number = 4;
  asserteq(p.number, 4);
}
