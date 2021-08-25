#pragma once
#include "core_dynstring.h"

/**
 * A single unicode codepoint.
 * https://en.wikipedia.org/wiki/Unicode#Architecture_and_terminology
 */
typedef u32 Utf8Codepoint;

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
usize utf8_cp_bytes(Utf8Codepoint);

/**
 * Write a utf8 string (1 - 4 bytes) for a Unicode codepoint.
 */
void utf8_cp_write(DynString*, Utf8Codepoint);
