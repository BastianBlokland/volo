#pragma once
#include "core_symbol.h"

typedef struct sSymbolReg SymbolReg;

void symbol_reg_set_offset(SymbolReg*, SymbolAddrRel addrOffset);
void symbol_reg_add(SymbolReg*, SymbolAddrRel begin, SymbolAddrRel end, String name);

/**
 * Pre-load debug symbols so they are ready when calling a symbol_dbg_*() api.
 */
void symbol_dbg_preload(void);

SymbolAddr symbol_pal_prog_begin(void);
SymbolAddr symbol_pal_prog_end(void);
void       symbol_pal_dbg_init(SymbolReg*);
