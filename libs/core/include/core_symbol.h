#pragma once
#include "core_string.h"

typedef void* Symbol;

/**
 * Lookup the name of the given symbol, returns an empty string if no name was found.
 * NOTE: Requires the executable to be compiled with debug symbols.
 */
String symbol_name(Symbol);
