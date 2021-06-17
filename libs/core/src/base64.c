#include "core_alloc.h"
#include "core_array.h"
#include "core_base64.h"

/**
 * Mapping of ascii characters, starting at '+' and ending with 'z' to the base64 table.
 * The base64 table can be found on the wiki page: https://en.wikipedia.org/wiki/Base64
 * Note: value of 255 indicates the ascii character is not a valid base64 char.
 */
static u8 g_decodeTable[] = {62,  255, 255, 255, 63,  52,  53, 54, 55, 56, 57, 58, 59, 60, 61, 255,
                             255, 255, 255, 255, 255, 255, 0,  1,  2,  3,  4,  5,  6,  7,  8,  9,
                             10,  11,  12,  13,  14,  15,  16, 17, 18, 19, 20, 21, 22, 23, 24, 25,
                             255, 255, 255, 255, 255, 255, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35,
                             36,  37,  38,  39,  40,  41,  42, 43, 44, 45, 46, 47, 48, 49, 50, 51};
_Static_assert(sizeof(g_decodeTable) == 'z' - '+' + 1, "Incorrect decode table size");

usize base64_decoded_size(String encoded) {
  if (encoded.size < 2) {
    // Needs atleast 2 base64 chars to represent a single byte.
    return 0;
  }
  // Check how many padding characters there are. Either 2, 1 or 0.
  const u8 padding = *(string_end(encoded) - 2) == '='   ? 2U
                     : *(string_end(encoded) - 1) == '=' ? 1U
                                                         : 0U;
  return encoded.size / 4 * 3 - padding;
}

void base64_decode(DynString* str, String encoded) {
  /**
   * Implemention based on awnser of 'nunojpg' in the so question:
   * https://stackoverflow.com/questions/180947/base64-decode-snippet-in-c
   */
  u32 val     = 0;
  i32 valBits = -8; // 0 indicates we have a 'full' 8 bit value in 'val'.
  mem_for_u8(encoded, c, {
    if (c < '+' || c > 'z') {
      break; // Non base64 characters found: abort.
    }
    if (g_decodeTable[c - '+'] == 255) {
      break; // Non base64 character found: abort.
    }
    // Each Base64 digit contains 6 bits of data, shift the current value over by 6 and put the new
    // data in the least significant bits.
    val = (val << 6U) | g_decodeTable[c - '+'];
    valBits += 6; // Indicate that we have 6 more bits 'available'.
    if (valBits >= 0) {
      // We have enough bits to form a byte.
      dynstring_append_char(str, (u8)(val >> valBits)); // Shift-of any excess bits.
      valBits -= 8;
    }
  });
}

String base64_decode_scratch(String encoded) {
  const usize decodedSize = base64_decoded_size(encoded);
  if (!decodedSize) {
    return string_empty;
  }

  Mem       scratchMem = alloc_alloc(g_alloc_scratch, decodedSize, 1);
  DynString str        = dynstring_create_over(scratchMem);

  base64_decode(&str, encoded);

  String res = dynstring_view(&str);
  dynstring_destroy(&str);
  return res;
}
