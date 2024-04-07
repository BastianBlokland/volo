#include "core_array.h"
#include "core_diag.h"

#include "dynlib_internal.h"

void dynlib_init() { dynlib_pal_init(); }

static const String g_dynlibResultStrs[] = {
    string_static("DynLibSuccess"),
    string_static("DynLibLibraryNameTooLong"),
    string_static("DynLibLibraryNotFound"),
    string_static("DynLibUnknownError"),
};

ASSERT(array_elems(g_dynlibResultStrs) == DynLibResult_Count, "Incorrect number of result names");

String dynlib_result_str(const DynLibResult result) {
  diag_assert(result < DynLibResult_Count);
  return g_dynlibResultStrs[result];
}

DynLibResult dynlib_load(Allocator* alloc, const String name, DynLib** out) {
  return dynlib_pal_load(alloc, name, out);
}

void dynlib_destroy(DynLib* lib) { dynlib_pal_destroy(lib); }

String dynlib_path(const DynLib* lib) { return dynlib_pal_path(lib); }

DynLibSymbol dynlib_symbol(const DynLib* lib, const String name) {
  return dynlib_pal_symbol(lib, name);
}
