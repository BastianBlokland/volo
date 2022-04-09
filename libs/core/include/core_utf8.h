#pragma once
#include "core_dynstring.h"
#include "core_unicode.h"

/**
 * Test if the given byte is a utf8 continuation byte.
 */
bool utf8_contchar(u8);

/**
 * Count the amount of unicode code-points in the given (utf8) string.
 * Pre-condition: Input string is valid utf8.
 */
usize utf8_cp_count(String);

/**
 * Returns how many utf8 bytes are required to represent the given codepoint.
 */
usize utf8_cp_bytes(Unicode);

/**
 * Given a utf8 starting character, returns the total amount of utf8 bytes for the codepoint.
 * NOTE: Returns 0 when the given character is not a valid utf8 starting character.
 */
usize utf8_cp_bytes_from_first(u8);

/**
 * Write a utf8 string (1 - 4 bytes) for a Unicode codepoint.
 */
void utf8_cp_write(DynString*, Unicode);

/**
 * Read the next Unicode codepoint from the given utf8 string.
 * Returns the remaining utf8 string.
 * NOTE: Returns the Unicode NULL (0) character when invalid utf8 is detected.
 */
String utf8_cp_read(String, Unicode* out);
