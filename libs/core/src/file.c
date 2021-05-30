#include "core_array.h"
#include "core_diag.h"
#include "core_file.h"
#include "file_internal.h"

static const String g_file_result_strs[] = {
    string_lit("FileSuccess"),
    string_lit("FileAlreadyExists"),
    string_lit("FileDiskFull"),
    string_lit("FileInvalidFilename"),
    string_lit("FileLocked"),
    string_lit("FileNoAccess"),
    string_lit("FileNoDataAvailable"),
    string_lit("FileNotFound"),
    string_lit("FilePathTooLong"),
    string_lit("FilePathInvalid"),
    string_lit("FileTooManyOpen"),
    string_lit("FileUnknownError"),
};

diag_static_assert(
    array_elems(g_file_result_strs) == FileResult_Count, "Incorrect number of FileResult strings");

String file_result_str(FileResult result) {
  diag_assert(result < FileResult_Count);
  return g_file_result_strs[result];
}

void file_init() { file_pal_init(); }
