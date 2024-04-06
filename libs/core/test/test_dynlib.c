#include "check_spec.h"
#include "core_alloc.h"
#include "core_dynlib.h"

spec(dynlib) {

  it("fails when opening an non-existent library") {
    Allocator*   alloc   = alloc_bump_create_stack(512);
    const String libName = string_lit("non-existent-library");

    DynLib* lib;
    check(dynlib_load(alloc, libName, &lib) == DynLibResult_LibraryNotFound);
  }
}
