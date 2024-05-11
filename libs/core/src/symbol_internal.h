#pragma once
#include "core_symbol.h"

typedef struct sSymbolReg SymbolReg;

void symbol_reg_add(SymbolReg*, SymbolAddrRel begin, SymbolAddrRel end, String name);

SymbolAddr symbol_pal_prog_begin(void);
SymbolAddr symbol_pal_prog_end(void);
void       symbol_pal_dbg_init(SymbolReg*);
