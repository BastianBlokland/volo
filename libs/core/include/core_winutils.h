#pragma once
#include "core_string.h"

#if defined(VOLO_WIN32)

/**
 * Returns the size (in bytes) required to store the input string (assumed to be utf8) as a
 * null-terminated wide string (as is commonly used in win32 apis).
 * NOTE: Returns 'sentinel_usize' if the input is invalid utf8.
 *
 * Pre-condition: !string_is_empty(input)
 */
usize winutils_to_widestr_size(String input);

/**
 * Convert a string (assumed to be utf8) to a null-terminated wide string (as is commonly used in
 * win32 apis).
 * Returns the amount of wide characters written to the output buffer.
 * NOTE: Returns 'sentinel_usize' if the input is invalid utf8 or the output buffer is too small.
 *
 * Pre-condition: !string_is_empty(input)
 */
usize winutils_to_widestr(Mem output, String input);

/**
 * Returns the size (in bytes) required to store the input wide string (as is commonly used in
 * win32 apis) as utf8.
 * NOTE: Returns 'sentinel_usize' if the input cannot be represented as utf8.
 *
 * Pre-condition: inputCharCount != 0
 */
usize winutils_from_widestr_size(void* input, usize inputCharCount);

/**
 * Convert a wide string (as is commonly used in win32 apis) to a string (encoded as utf8).
 * Returns the amount of utf8 bytes written to the output string.
 * NOTE: Returns 'sentinel_usize' if the input cannot be represented as utf8 or the output string is
 * too small.
 *
 * Pre-condition: inputCharCount != 0
 */
usize winutils_from_widestr(String output, void* input, usize inputCharCount);

#endif
