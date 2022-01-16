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
usize utf8_cp_bytes(UnicodeCp);

/**
 * Write a utf8 string (1 - 4 bytes) for a Unicode codepoint.
 */
void utf8_cp_write(DynString*, UnicodeCp);
