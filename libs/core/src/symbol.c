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

bool symbol_addr_valid(const SymbolAddr symbol) {
  return symbol >= g_symProgramStart && symbol < g_symProgramEnd;
}

SymbolAddrRel symbol_addr_rel(const SymbolAddr symbol) {
  if (!symbol_addr_valid(symbol)) {
    return sentinel_u32;
  }
  return (SymbolAddrRel)(symbol - g_symProgramStart);
}

SymbolAddr symbol_addr_abs(const SymbolAddrRel addr) {
  if (sentinel_check(addr)) {
    return 0;
  }
  return (SymbolAddr)addr + g_symProgramStart;
}

String symbol_name(const SymbolAddr addr) {
  if (!symbol_addr_valid(addr)) {
    return string_empty;
  }
  const SymbolAddrRel addrRel = symbol_addr_rel(addr);
  return symbol_pal_name(addrRel);
}

String symbol_name_rel(const SymbolAddrRel addr) {
  if (sentinel_check(addr)) {
    return string_empty;
  }
  return symbol_pal_name(addr);
}
