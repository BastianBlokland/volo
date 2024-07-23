#include "core_alloc.h"
#include "core_array.h"
#include "core_base64.h"
#include "core_bits.h"

/**
 * Table of Base64 characters.
 * For the source see the wiki page: https://en.wikipedia.org/wiki/Base64
 */
static const u8 g_encodeTable[] = {
    'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L', 'M', 'N', 'O', 'P',
    'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X', 'Y', 'Z', 'a', 'b', 'c', 'd', 'e', 'f',
    'g', 'h', 'i', 'j', 'k', 'l', 'm', 'n', 'o', 'p', 'q', 'r', 's', 't', 'u', 'v',
    'w', 'x', 'y', 'z', '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', '+', '/',
};
ASSERT(sizeof(g_encodeTable) == 64, "Incorrect encode table size");

/**
 * Mapping of ascii characters, starting at '+' and ending with 'z' to the base64 table.
 * NOTE: value of 255 indicates the ascii character is not a valid base64 char.
 */
static const u8 g_decodeTable[] = {
    62,  255, 255, 255, 63, 52, 53, 54, 55,  56,  57,  58,  59,  60,  61, 255, 255, 255, 255, 255,
    255, 255, 0,   1,   2,  3,  4,  5,  6,   7,   8,   9,   10,  11,  12, 13,  14,  15,  16,  17,
    18,  19,  20,  21,  22, 23, 24, 25, 255, 255, 255, 255, 255, 255, 26, 27,  28,  29,  30,  31,
    32,  33,  34,  35,  36, 37, 38, 39, 40,  41,  42,  43,  44,  45,  46, 47,  48,  49,  50,  51,
};
ASSERT(sizeof(g_decodeTable) == 'z' - '+' + 1, "Incorrect decode table size");

usize base64_encoded_size(const String data) { return (data.size + 2) / 3 * 4; }

usize base64_decoded_size(const String encoded) {
  if (encoded.size < 2) {
    return 0; // Needs atleast 2 base64 chars to represent a single byte.
  }
  // Check how many padding characters there are. Either 2, 1 or 0.
  u8 padding;
  if (*(string_end(encoded) - 2) == '=') {
    padding = 2;
  } else if (*(string_end(encoded) - 1) == '=') {
    padding = 1;
  } else {
    padding = 0;
  }
  return encoded.size / 4 * 3 - padding;
}

void base64_encode(DynString* str, const String data) {
  /**
   * Implementation based on answer of 'Manuel Martinez' in the so question:
   * https://stackoverflow.com/questions/180947/base64-decode-snippet-in-c
   */
  u32 val          = 0;
  u32 valBits      = 0; // 6 indicates we have a full value in 'val'.
  u32 bytesWritten = 0;
  mem_for_u8(data, itr) {
    val = (val << 8) + *itr;
    valBits += 8;
    while (valBits >= 6) {
      valBits -= 6;
      dynstring_append_char(str, g_encodeTable[(val >> valBits) & 0x3F]); // Shift away excess bits.
      ++bytesWritten;
    }
  }
  if (valBits) {
    dynstring_append_char(str, g_encodeTable[((val << 8) >> (valBits + 2)) & 0x3F]);
    ++bytesWritten;
  }
  dynstring_append_chars(str, '=', bits_padding_32(bytesWritten, 4));
}

void base64_decode(DynString* str, const String encoded) {
  /**
   * Implementation based on answer of 'nunojpg' in the so question:
   * https://stackoverflow.com/questions/180947/base64-decode-snippet-in-c
   */
  u32 val     = 0;
  u32 valBits = 0; // 8 indicates we have a full value in 'val'.
  mem_for_u8(encoded, itr) {
    if (*itr < '+' || *itr > 'z') {
      break; // Non base64 characters found: abort.
    }
    const u32 tableIndex = *itr - '+';
    if (g_decodeTable[tableIndex] == 255) {
      break; // Non base64 character found: abort.
    }
    // Each Base64 digit contains 6 bits of data, shift the current value over by 6 and put the new
    // data in the least significant bits.
    val = (val << 6) | g_decodeTable[tableIndex];
    valBits += 6; // Indicate that we have 6 more bits 'available'.
    if (valBits >= 8) {
      // We have enough bits to form a byte.
      valBits -= 8;
      dynstring_append_char(str, (u8)(val >> valBits)); // Shift away excess bits.
    }
  }
}

String base64_encode_scratch(const String data) {
  const usize encodedSize = base64_encoded_size(data);
  if (!encodedSize) {
    return string_empty;
  }

  Mem       scratchMem = alloc_alloc(g_allocScratch, encodedSize, 1);
  DynString str        = dynstring_create_over(scratchMem);

  base64_encode(&str, data);

  String res = dynstring_view(&str);
  dynstring_destroy(&str);
  return res;
}

String base64_decode_scratch(const String encoded) {
  const usize decodedSize = base64_decoded_size(encoded);
  if (!decodedSize) {
    return string_empty;
  }

  Mem       scratchMem = alloc_alloc(g_allocScratch, decodedSize, 1);
  DynString str        = dynstring_create_over(scratchMem);

  base64_decode(&str, encoded);

  String res = dynstring_view(&str);
  dynstring_destroy(&str);
  return res;
}
