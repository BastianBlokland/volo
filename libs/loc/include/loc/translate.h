#pragma once
#include "core/forward.h"

#define loc_translate_lit(_KEY_LIT_) loc_translate(string_hash_lit(_KEY_LIT_))
#define loc_translate_str(_KEY_STR_) loc_translate(string_hash(_KEY_STR_))

/**
 * NOTE: Resulting string is allocated in scratch memory.
 */
#define loc_translate_fmt(_KEY_, ...)                                                              \
  format_write_formatted_scratch(loc_translate(_KEY_), fmt_args(__VA_ARGS__))

/**
 * NOTE: Resulting string is allocated in scratch memory.
 */
#define loc_translate_lit_fmt(_KEY_LIT_, ...)                                                      \
  format_write_formatted_scratch(loc_translate(string_hash_lit(_KEY_LIT_)), fmt_args(__VA_ARGS__))

/**
 * Retrieve the localized string for the given key.
 * NOTE: Returns an empty string when the key cannot be found.
 * NOTE: Returned string is valid until the end of the frame.
 */
String loc_translate(StringHash key);
