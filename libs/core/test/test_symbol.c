#include "check_spec.h"
#include "core_annotation.h"
#include "core_symbol.h"

static volatile i32 g_preserveFunc;

NO_INLINE_HINT static void test_symbol_func() { g_preserveFunc = 42; }

static bool test_symbol_stack_func1();
static bool test_symbol_stack_func2();
static bool test_symbol_stack_func3();

NO_INLINE_HINT static bool test_symbol_stack_func1() { return test_symbol_stack_func2() ^ true; }
NO_INLINE_HINT static bool test_symbol_stack_func2() { return test_symbol_stack_func3() ^ true; }

NO_INLINE_HINT static bool test_symbol_stack_func3() {
  const SymbolStack stack = symbol_stack();
  // The topmost three frames should be the test functions.
  if (symbol_base(stack.frames[0]) != symbol_addr_rel((SymbolAddr)&test_symbol_stack_func3)) {
    return false;
  }
  if (symbol_base(stack.frames[1]) != symbol_addr_rel((SymbolAddr)&test_symbol_stack_func2)) {
    return false;
  }
  if (symbol_base(stack.frames[2]) != symbol_addr_rel((SymbolAddr)&test_symbol_stack_func1)) {
    return false;
  }
  return true;
}

static CheckTestFlags test_requires_dbg_info_flags(void) {
  CheckTestFlags flags = CheckTestFlags_None;
#ifdef __MINGW32__
  flags |= CheckTestFlags_Skip; // MinGW (gcc port for Windows) doesn't emit PDB files at this time.
#endif
  // TODO: Skip if the executable was build without debug information?
  return flags;
}

spec(symbol) {

  it("returns an empty string for a non-existent function") {
    check_eq_string(symbol_name((SymbolAddr)42), string_empty);
    check_eq_string(symbol_name((SymbolAddr)uptr_max), string_empty);
  }

  it("can lookup the name of a function", .flags = test_requires_dbg_info_flags()) {
    // NOTE: Requires the test executable to be build with debug info.
    check_eq_string(symbol_name((SymbolAddr)&test_symbol_func), string_lit("test_symbol_func"));
  }

  it("can lookup the base address of a function", .flags = test_requires_dbg_info_flags()) {
    // NOTE: Requires the test executable to be build with debug info.
    const SymbolAddr    addr    = (SymbolAddr)&test_symbol_func;
    const SymbolAddrRel addrRel = symbol_addr_rel(addr);
    check_eq_int(symbol_base(addrRel), addrRel);
    check_eq_int(symbol_base(addrRel + 4), addrRel);
  }

  it("can collect stack traces", .flags = test_requires_dbg_info_flags()) {
    // NOTE: Requires the test executable to be build with debug info.
    check(test_symbol_stack_func1());
  }
}
