#include "symbol_internal.h"

static SymbolAddr g_symProgramStart;
static SymbolAddr g_symProgramEnd;

void symbol_init(void) {
  symbol_pal_init();

  g_symProgramStart = symbol_pal_program_start();
  g_symProgramEnd   = symbol_pal_program_end();
}

void symbol_teardown(void) { symbol_pal_teardown(); }

String symbol_name(Symbol symbol) { return symbol_pal_name(symbol); }
