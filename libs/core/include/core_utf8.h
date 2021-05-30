#pragma once
#include "core_string.h"

/**
 * Test if the given byte is a utf8 continuation byte.
 */
bool utf8_contchar(u8);

/**
 * Count the amount of unicode code-points in the given (utf8) string.
 * Pre-condition: Input string is valid utf8.
 */
usize utf8_cp_count(String);
