#pragma once
#include "core_dynstring.h"
#include "core_types.h"

/**
 * MIME Base64 encoding uses 64 'safe' ascii characters to represent 6 bits of data.
 * So to represent 3 bytes of data you need 4 base64 digits (24 bit = 6 * 4).
 *
 * The base64 characters are: 'A - Z', 'a - z' and '0 - 9' and '+' and '/'.
 * '=' is used to pad to a multiple of 4.
 */

/**
 * Calculate how many bytes the decoded output will be.
 * Pre-condition: 'encoded' is validly encoded and padded base64.
 */
usize base64_decoded_size(String encoded);

/**
 * Decode MIME Base64 encoded input.
 * Pre-condition: 'encoded' is validly encoded and padded base64.
 */
void base64_decode(DynString* str, String encoded);

/**
 * Decode MIME Base64 encoded input in scratch memory.
 * Pre-condition: 'encoded' is validly encoded and padded base64.
 * Pre-condition: base64_decoded_size(encoded) < 8KiB.
 */
String base64_decode_scratch(String encoded);
