#include "core_array.h"
#include "core_diag.h"
#include "core_file.h"

static const String g_file_result_strs[] = {
    string_lit("File_Success"),
    string_lit("File_NoDataAvailable"),
    string_lit("File_DiskFull"),
    string_lit("File_NotFound"),
    string_lit("File_NoAccess"),
    string_lit("File_Locked"),
    string_lit("File_TooManyOpenFiles"),
    string_lit("File_UnknownError"),
};

diag_static_assert(
    array_elems(g_file_result_strs) == File_ResultCount, "Incorrect number of FileResult strings");

String file_result_str(FileResult result) {
  diag_assert(result < File_ResultCount);
  return g_file_result_strs[result];
}
