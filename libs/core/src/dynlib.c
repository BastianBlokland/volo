#include "core_array.h"
#include "core_diag.h"
#include "core_dynlib.h"

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
