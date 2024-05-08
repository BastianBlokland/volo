#include "symbol_internal.h"

void   symbol_init(void) { symbol_pal_init(); }
void   symbol_teardown(void) { symbol_pal_teardown(); }
String symbol_name(Symbol symbol) { return symbol_pal_name(symbol); }
