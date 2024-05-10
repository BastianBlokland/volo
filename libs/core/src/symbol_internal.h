#pragma once
#include "core_symbol.h"

void       symbol_pal_init(void);
void       symbol_pal_teardown(void);
SymbolAddr symbol_pal_program_start(void);
SymbolAddr symbol_pal_program_end(void);
String     symbol_pal_name(Symbol);
