#pragma once
#include "core_string.h"

typedef void* Symbol;
typedef uptr  SymbolAddr;
typedef u32   SymbolAddrRel; // Relative to program base (limits executable size to 4 GiB).

/**
 * Utilities for converting between relative and absolute addresses.
 * NOTE: Only works for symbols contained in the executable itself, not for dynamic library symbols.
 */
bool          symbol_addr_valid(SymbolAddr);
SymbolAddrRel symbol_addr_rel(SymbolAddr);
SymbolAddr    symbol_addr_abs(SymbolAddrRel);

/**
 * Lookup the name of the symbol that contains the given address.
 * NOTE: returns an empty string if no name was found.
 *
 * Pre-condition: Executable contains debug information.
 * Pre-condition: Address is contained in a (non-inlined) function in the executable itself.
 */
String symbol_name(SymbolAddr);
String symbol_name_rel(SymbolAddrRel);
