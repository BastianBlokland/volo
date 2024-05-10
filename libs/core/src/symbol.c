#include "core_sentinel.h"

#include "symbol_internal.h"

static SymbolAddr g_symProgramStart;
static SymbolAddr g_symProgramEnd;

void symbol_init(void) {
  symbol_pal_init();

  g_symProgramStart = symbol_pal_program_start();
  g_symProgramEnd   = symbol_pal_program_end();
}

void symbol_teardown(void) { symbol_pal_teardown(); }

bool symbol_valid(Symbol symbol) {
  return (SymbolAddr)symbol >= g_symProgramStart && (SymbolAddr)symbol < g_symProgramEnd;
}

SymbolAddrRel symbol_addr_rel(Symbol symbol) {
  return (SymbolAddrRel)((SymbolAddr)symbol - g_symProgramStart);
}

SymbolAddr symbol_addr_abs(SymbolAddrRel addr) { return (SymbolAddr)addr + g_symProgramStart; }

String symbol_name(Symbol symbol) {
  if (!symbol_valid(symbol)) {
    return string_empty;
  }
  const SymbolAddrRel addr = symbol_addr_rel(symbol);
  return symbol_pal_name(addr);
}

String symbol_name_from_rel(const SymbolAddrRel addr) {
  if (sentinel_check(addr)) {
    return string_empty;
  }
  return symbol_pal_name(addr);
}
