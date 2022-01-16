#include "core_annotation.h"
#include "core_dynstring.h"
#include "core_utf8.h"

#define utf8_cp_max ((UnicodeCp)0x10FFFF)
#define utf8_cp_single_char ((UnicodeCp)0x7F)
#define utf8_cp_double_char ((UnicodeCp)0x7FF)
#define utf8_cp_triple_char ((UnicodeCp)0xFFFF)
#define utf8_cp_quad_char utf8_cp_max

static bool utf8_cp_valid(const UnicodeCp cp) { return cp <= utf8_cp_max; }

static u8 utf8_charcount_from_first(const u8 c) {
  if ((c & 0b10000000) == 0) {
    return 1;
  }
  if ((c & 0b11100000) == 0b11000000) {
    return 2;
  }
  if ((c & 0b11110000) == 0b11100000) {
    return 3;
  }
  if ((c & 0b11111000) == 0b11110000) {
    return 4;
  }
  return 0; // Invalid utf8 char.
}

bool utf8_contchar(const u8 c) { return (c & 0b11000000) == 0b10000000; }

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

String utf8_cp_read(String utf8, UnicodeCp* out) {
  if (UNLIKELY(!utf8.size)) {
    *out = 0;
    return string_empty;
  }
  u8* chars = string_begin(utf8);

  // Find out how many utf8 characters this codepoint consists.
  const u8 charCount = utf8_charcount_from_first(chars[0]);
  if (UNLIKELY(!charCount)) {
    *out = 0;
    return string_consume(utf8, 1);
  }

  // Validate that the remaning characters are all valid utf8 continuation characters.
  if (UNLIKELY(utf8.size < charCount)) {
    *out = 0;
    return string_empty;
  }
  for (u8 i = 1; i != charCount; ++i) {
    if (UNLIKELY(!utf8_contchar(chars[i]))) {
      *out = 0;
      return string_consume(utf8, charCount);
    }
  }

  // Decode the Unicode codepoint.
  switch (charCount) {
  case 1:
    *out = chars[0];
    break;
  case 2:
    *out = (chars[0] & 0b00011111) << 6 | (chars[1] & 0b00111111);
    break;
  case 3:
    *out = (chars[0] & 0b00001111) << 12 | (chars[1] & 0b00111111) << 6 | (chars[2] & 0b00111111);
    break;
  case 4:
    *out = (chars[0] & 0b00000111) << 18 | (chars[1] & 0b00111111) << 12 |
           (chars[2] & 0b00111111) << 6 | (chars[3] & 0b00111111);
    break;
  }
  return string_consume(utf8, charCount);
}
