#include "core_array.h"
#include "core_diag.h"
#include "core_file.h"

#include "file_internal.h"
#include "init_internal.h"

static const String g_file_result_strs[] = {
    string_static("FileSuccess"),
    string_static("FileAlreadyExists"),
    string_static("FileDiskFull"),
    string_static("FileInvalidFilename"),
    string_static("FileLocked"),
    string_static("FileNoAccess"),
    string_static("FileNoDataAvailable"),
    string_static("FileNotFound"),
    string_static("FilePathTooLong"),
    string_static("FilePathInvalid"),
    string_static("FileTooManyOpen"),
    string_static("FileUnknownError"),
};

_Static_assert(
    array_elems(g_file_result_strs) == FileResult_Count, "Incorrect number of FileResult strings");

String file_result_str(FileResult result) {
  diag_assert(result < FileResult_Count);
  return g_file_result_strs[result];
}

void file_init() { file_pal_init(); }
