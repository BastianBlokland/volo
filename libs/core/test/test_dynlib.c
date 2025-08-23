#include "check/spec.h"
#include "core/alloc.h"
#include "core/dynlib.h"

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

#ifndef VOLO_WIN32 // NOTE: On Windows we statically link libc making this hard to test.

  it("can lookup global symbols") {
    double SYS_DECL sin(double); // Libc sin function.
    check_eq_int(dynlib_symbol_global(string_lit("sin")), &sin);
  }

#endif
}
