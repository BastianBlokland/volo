#pragma once
#include "core/forward.h"

/**
 * Retrieve the localized string for the given key.
 * NOTE: Returns an empty string when the locale resources are still being loaded.
 * NOTE: Returns the input if the key was not found in the locale resource.
 */
String loc_translate(String key);
