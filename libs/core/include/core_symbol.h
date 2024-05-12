#pragma once
#include "core_string.h"

// Forward declare from 'core_dynstring.h'.
typedef struct sDynArray DynString;

typedef void* Symbol;
typedef uptr  SymbolAddr;
typedef u32   SymbolAddrRel; // Relative to program base (limits executable size to 4 GiB).

typedef struct sSymbolStack {
  SymbolAddrRel frames[8]; // NOTE: Addresses are inside functions, not entry-points.
} SymbolStack;

/**
 * Collect the active stack frames.
 * NOTE: Only contains frames from our own executable, not from dynamic libraries.
 * NOTE: Has a fixed frame limit, for too deep stacks it only contains the topmost frames.
 * NOTE: Unused frames are set to sentinel_u32.
 *
 * Pre-condition: Executable is build with frame-pointers.
 */
SymbolStack symbol_stack_walk(void);

/**
 * Pretty print the stack to the given string.
 * NOTE: Requires debug information for pretty names / function base addresses.
 */
void symbol_stack_write(const SymbolStack*, DynString* out);

/**
 * Utilities for converting between relative and absolute addresses.
 * NOTE: Only works for symbols contained in the executable itself, not for dynamic library symbols.
 * Returns sentinel_u32 / sentinel_uptr for symbols not contained in the executable.
 */
SymbolAddrRel symbol_addr_rel(SymbolAddr);
SymbolAddrRel symbol_addr_rel_ptr(Symbol);
SymbolAddr    symbol_addr_abs(SymbolAddrRel);

/**
 * Lookup the name of the symbol that contains the given address.
 * NOTE: returns an empty string if no name was found.
 *
 * Pre-condition: Executable contains debug information.
 * Pre-condition: Address is contained in a (non-inlined) function in the executable itself.
 */
String symbol_dbg_name(SymbolAddrRel);

/**
 * Lookup the base address of the symbol at the given address.
 * NOTE: returns sentinel_u32 if the symbol could not be found.
 *
 * Pre-condition: Executable contains debug information.
 * Pre-condition: Address is contained in a (non-inlined) function in the executable itself.
 */
SymbolAddrRel symbol_dbg_base(SymbolAddrRel);
