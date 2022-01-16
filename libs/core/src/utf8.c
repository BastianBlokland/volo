#include "core_dynstring.h"
#include "core_utf8.h"

#define utf8_cp_max ((UnicodeCp)0x10FFFF)
#define utf8_cp_single_char ((UnicodeCp)0x7F)
#define utf8_cp_double_char ((UnicodeCp)0x7FF)
#define utf8_cp_triple_char ((UnicodeCp)0xFFFF)
#define utf8_cp_quad_char utf8_cp_max

static bool utf8_cp_valid(const UnicodeCp cp) { return cp <= utf8_cp_max; }

bool utf8_contchar(u8 c) { return (c & 0b11000000) == 0b10000000; }

usize utf8_cp_count(String str) {
  usize result = 0;
  mem_for_u8(str, itr) {
    if (!utf8_contchar(*itr)) {
      ++result;
    }
  }
  return result;
}

usize utf8_cp_bytes(const UnicodeCp cp) {
  if (cp <= utf8_cp_single_char) {
    return 1;
  }
  if (cp <= utf8_cp_double_char) {
    return 2;
  }
  if (cp <= utf8_cp_triple_char) {
    return 3;
  }
  return 4;
}

void utf8_cp_write(DynString* str, const UnicodeCp cp) {
  /**
   * Encode a Unicode codepoint as either 1, 2, 3 or 4 bytes.
   * Description of the encoding: https://en.wikipedia.org/wiki/UTF-8#Encoding
   */
  if (!utf8_cp_valid(cp)) {
    // Unicode replacement char encoded as utf8.
    dynstring_append_char(str, 0xEF);
    dynstring_append_char(str, 0xBF);
    dynstring_append_char(str, 0xBD);
    return;
  }
  if (cp <= utf8_cp_single_char) {
    dynstring_append_char(str, (u8)cp);
    return;
  }
  if (cp <= utf8_cp_double_char) {
    dynstring_append_char(str, (u8)(((cp >> 6) & 0x1F) | 0xC0));
    dynstring_append_char(str, (u8)((cp & 0x3F) | 0x80));
    return;
  }
  if (cp <= utf8_cp_triple_char) {
    dynstring_append_char(str, (u8)(((cp >> 12) & 0x0F) | 0xE0));
    dynstring_append_char(str, (u8)(((cp >> 6) & 0x3F) | 0x80));
    dynstring_append_char(str, (u8)((cp & 0x3F) | 0x80));
    return;
  }
  dynstring_append_char(str, (u8)(((cp >> 18) & 0x07) | 0xF0));
  dynstring_append_char(str, (u8)(((cp >> 12) & 0x3F) | 0x80));
  dynstring_append_char(str, (u8)(((cp >> 6) & 0x3F) | 0x80));
  dynstring_append_char(str, (u8)((cp & 0x3F) | 0x80));
}
