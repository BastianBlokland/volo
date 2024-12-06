#include "core.h"
#include "core_bits.h"
#include "core_dynstring.h"
#include "core_string.h"
#include "core_utf8.h"

#define utf8_cp_max ((Unicode)0x10FFFF)
#define utf8_cp_single_char ((Unicode)0x7F)
#define utf8_cp_double_char ((Unicode)0x7FF)
#define utf8_cp_triple_char ((Unicode)0xFFFF)
#define utf8_cp_quad_char utf8_cp_max

INLINE_HINT static String utf8_consume_bytes(const String str, const usize amount) {
  return (String){
      .ptr  = bits_ptr_offset(str.ptr, amount),
      .size = str.size - amount,
  };
}

INLINE_HINT static bool utf8_cp_valid(const Unicode cp) { return cp <= utf8_cp_max; }

INLINE_HINT static bool utf8_contchar_internal(const u8 c) {
  return (c & 0b11000000) == 0b10000000;
}

bool utf8_contchar(const u8 c) { return utf8_contchar_internal(c); }

bool utf8_validate(const String str) {
  const u8* chars    = string_begin(str);
  const u8* charsEnd = string_end(str);
  while (chars != charsEnd) {
    const usize charCount = utf8_cp_bytes_from_first(chars[0]);
    if (UNLIKELY((usize)(charsEnd - chars) < charCount)) {
      return false; // Not enough characters left for this code-point.
    }
    switch (charCount) {
    case 4:
      if (UNLIKELY(!utf8_contchar_internal(chars[3]))) {
        return false; // Invalid continuation character.
      }
      // Fallthrough.
    case 3:
      if (UNLIKELY(!utf8_contchar_internal(chars[2]))) {
        return false; // Invalid continuation character.
      }
      // Fallthrough.
    case 2:
      if (UNLIKELY(!utf8_contchar_internal(chars[1]))) {
        return false; // Invalid continuation character.
      }
      // Fallthrough.
    case 1:
      break; // Valid code-point.
    case 0:
      return false; // Invalid starting character.
    default:
      UNREACHABLE
    }
    chars += charCount;
  }
  return true;
}

usize utf8_cp_count(const String str) {
  usize result = 0;
  mem_for_u8(str, itr) {
    if (!utf8_contchar_internal(*itr)) {
      ++result;
    }
  }
  return result;
}

usize utf8_cp_bytes(const Unicode cp) {
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

usize utf8_cp_bytes_from_first(const u8 c) {
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

usize utf8_cp_write(u8 buffer[PARAM_ARRAY_SIZE(4)], const Unicode cp) {
  /**
   * Encode a Unicode codepoint as either 1, 2, 3 or 4 bytes.
   * Description of the encoding: https://en.wikipedia.org/wiki/UTF-8#Encoding
   */
  if (!utf8_cp_valid(cp)) {
    // Unicode replacement char encoded as utf8.
    buffer[0] = 0xEF;
    buffer[1] = 0xBF;
    buffer[2] = 0xBD;
    return 3;
  }
  if (cp <= utf8_cp_single_char) {
    buffer[0] = (u8)cp;
    return 1;
  }
  if (cp <= utf8_cp_double_char) {
    buffer[0] = (u8)(((cp >> 6) & 0x1F) | 0xC0);
    buffer[1] = (u8)((cp & 0x3F) | 0x80);
    return 2;
  }
  if (cp <= utf8_cp_triple_char) {
    buffer[0] = (u8)(((cp >> 12) & 0x0F) | 0xE0);
    buffer[1] = (u8)(((cp >> 6) & 0x3F) | 0x80);
    buffer[2] = (u8)((cp & 0x3F) | 0x80);
    return 3;
  }
  buffer[0] = (u8)(((cp >> 18) & 0x07) | 0xF0);
  buffer[1] = (u8)(((cp >> 12) & 0x3F) | 0x80);
  buffer[2] = (u8)(((cp >> 6) & 0x3F) | 0x80);
  buffer[3] = (u8)((cp & 0x3F) | 0x80);
  return 4;
}

void utf8_cp_write_to(DynString* str, const Unicode cp) {
  const usize initialSize = str->size;
  u8*         buffer      = dynstring_push(str, 4).ptr;
  const usize charCount   = utf8_cp_write(buffer, cp);
  str->size               = initialSize + charCount;
}

String utf8_cp_read(String utf8, Unicode* out) {
  if (UNLIKELY(!utf8.size)) {
    *out = 0;
    return string_empty;
  }
  const u8* chars = string_begin(utf8);

  // Find out how many utf8 characters this codepoint consists.
  const usize charCount = utf8_cp_bytes_from_first(chars[0]);
  if (UNLIKELY(!charCount)) {
    *out = 0;
    return utf8_consume_bytes(utf8, 1);
  }

  // Validate that the remaining characters are all valid utf8 continuation characters.
  if (UNLIKELY(utf8.size < charCount)) {
    *out = 0;
    return string_empty;
  }
  for (u8 i = 1; i != charCount; ++i) {
    if (UNLIKELY(!utf8_contchar_internal(chars[i]))) {
      *out = 0;
      return utf8_consume_bytes(utf8, charCount);
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
  return utf8_consume_bytes(utf8, charCount);
}
