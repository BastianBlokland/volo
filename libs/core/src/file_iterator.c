#include "core/array.h"
#include "core/diag.h"
#include "core/file_iterator.h"

static const String g_fileIteratorResultStrs[] = {
    string_static("FileIteratorFound"),
    string_static("FileIteratorEnd"),
    string_static("FileIteratorNoAccess"),
    string_static("FileIteratorDirectoryDoesNotExist"),
    string_static("FileIteratorPathIsNotADirectory"),
    string_static("FileIteratorTooManyOpenFiles"),
    string_static("FileIteratorUnknownError"),
};

ASSERT(
    array_elems(g_fileIteratorResultStrs) == FileIteratorResult_Count,
    "Incorrect number of FileIteratorResult strings");

String file_iterator_result_str(const FileIteratorResult result) {
  diag_assert(result < FileIteratorResult_Count);
  return g_fileIteratorResultStrs[result];
}
