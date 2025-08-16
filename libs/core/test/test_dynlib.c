#include "check_spec.h"
#include "core_alloc.h"
#include "core_dynlib.h"

double SYS_DECL sin(double); // Libc sin function.

spec(dynlib) {

  it("fails when opening an non-existent library") {
    Allocator*   alloc   = alloc_bump_create_stack(512);
    const String libName = string_lit("non-existent-library");

    DynLib* lib;
    check(dynlib_load(alloc, libName, &lib) == DynLibResult_LibraryNotFound);
  }

  it("returns null if a global symbol cannot be found") {
    check(dynlib_symbol_global(string_lit("hello_world")) == null);
  }

  it("can lookup global symbols") {
    // Check if we can lookup the libc sin function.
    check(dynlib_symbol_global(string_lit("sin")) == &sin);
  }
}
