#include "rbase.h"

ASSUME_NONNULL_BEGIN


void fmthex(char* out, const u8* indata, int len) {
  const char* hex = "0123456789abcdef";
  for (int i = 0; i < len; i++) {
    out[0] = hex[(indata[i]>>4) & 0xF];
    out[1] = hex[ indata[i]     & 0xF];
    out += 2;
    // sprintf((char*)dst+2*i, "%02x", digest[i]);
  }
}

// DEPRECATED
const char* user_home_dir() { return os_user_home_dir(); }


ASSUME_NONNULL_END
