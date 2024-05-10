#pragma once
#include "core_string.h"

typedef void* Symbol;
typedef uptr  SymbolAddr;
typedef u32   SymbolAddrRel; // Relative to program base (limits executable size to 4 GiB).

/**
 * Utilities for converting between relative and absolute addresses.
 * NOTE: Only works for symbols contained in the executable itself, not for dynamic library symbols.
 */
bool          symbol_in_executable(Symbol);
SymbolAddrRel symbol_addr_rel(Symbol);
SymbolAddr    symbol_addr_abs(SymbolAddrRel);

/**
 * Lookup the name of the given symbol, returns an empty string if no name was found.
 *
 * Pre-condition: Executable contains debug information.
 * Pre-condition: Symbol points to a (non-inlined) function contained in the executable itself.
 */
String symbol_name(Symbol);
String symbol_name_from_rel(SymbolAddrRel);
