#include "check_spec.h"
#include "core_annotation.h"
#include "core_symbol.h"

volatile i32 g_preserveFunc;

spec(symbol);

NO_INLINE_HINT static void test_symbol_func() { g_preserveFunc = 42; }

NO_INLINE_HINT static bool test_symbol_stack_valid() {
  const SymbolStack stack = symbol_stack();
  /**
   * Topmost two frames should be this function and the 'symbol' spec routine.
   */
  if (symbol_base(stack.frames[0]) != symbol_addr_rel((SymbolAddr)&test_symbol_stack_valid)) {
    return false;
  }
  if (symbol_base(stack.frames[1]) != symbol_addr_rel((SymbolAddr)&spec_name(symbol))) {
    return false;
  }
  return true;
}

spec(symbol) {

  it("returns an empty string for a non-existent function") {
    check_eq_string(symbol_name((SymbolAddr)42), string_empty);
    check_eq_string(symbol_name((SymbolAddr)uptr_max), string_empty);
  }

  it("can lookup the name of a function") {
    // NOTE: Requires the test executable to be build with debug info.
    check_eq_string(symbol_name((SymbolAddr)&test_symbol_func), string_lit("test_symbol_func"));
  }

  it("can collect stack traces") { check(test_symbol_stack_valid()); }
}
