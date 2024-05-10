#pragma once
#include "core_string.h"

typedef void* Symbol;
typedef uptr  SymbolAddr;
typedef u32   SymbolAddrRel; // Relative to program base (limits executable size to 4 GiB).

/**
 * Lookup the name of the given symbol, returns an empty string if no name was found.
 *
 * Pre-condition: Executable contains debug information.
 * Pre-condition: Symbol points to a (non-inlined) function contained in the executable itself.
 */
String symbol_name(Symbol);
