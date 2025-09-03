#pragma once
#include "core/forward.h"

/**
 * Retrieve the localized string for the given key.
 * NOTE: Returns an empty string when the key cannot be found.
 */
String loc_translate(StringHash key);
