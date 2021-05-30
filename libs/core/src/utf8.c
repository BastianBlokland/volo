#include "core_utf8.h"

bool utf8_contchar(u8 c) { return (c & 0b11000000) == 0b10000000; }

usize utf8_cp_count(String str) {
  usize result = 0;
  mem_for_u8(str, byte, {
    if (!utf8_contchar(byte)) {
      ++result;
    }
  });
  return result;
}
