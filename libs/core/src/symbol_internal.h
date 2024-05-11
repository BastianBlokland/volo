#pragma once
#include "core_symbol.h"

void          symbol_pal_init(void);
void          symbol_pal_teardown(void);
SymbolAddr    symbol_pal_prog_begin(void);
SymbolAddr    symbol_pal_prog_end(void);
String        symbol_pal_dbg_name(SymbolAddrRel);
SymbolAddrRel symbol_pal_dbg_base(SymbolAddrRel);
