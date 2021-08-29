#pragma once
#include "json_doc.h"

/**
 * Check if two values are equal.
 *
 * Pre-condition: JsonVal x is valid in the given document.
 * Pre-condition: JsonVal y is valid in the given document.
 */
bool json_eq(JsonDoc*, JsonVal x, JsonVal y);
