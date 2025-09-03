#pragma once
#include "core/forward.h"

#define loc_translate_lit(_KEY_LIT_) loc_translate(string_hash_lit(_KEY_LIT_))
#define loc_translate_str(_KEY_STR_) loc_translate(string_hash(_KEY_STR_))

/**
 * Retrieve the localized string for the given key.
 * NOTE: Returns an empty string when the key cannot be found.
 * NOTE: Returned string is valid until the end of the frame.
 */
String loc_translate(StringHash key);
