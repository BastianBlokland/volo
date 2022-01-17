#include "core_alloc.h"
#include "core_array.h"
#include "core_diag.h"
#include "core_file.h"
#include "core_path.h"

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
    string_static("FileIsDirectory"),
    string_static("FileAllocationFailed"),
    string_static("FileEmpty"),
    string_static("FileUnknownError"),
};

ASSERT(
    array_elems(g_file_result_strs) == FileResult_Count, "Incorrect number of FileResult strings");

String file_result_str(const FileResult result) {
  diag_assert(result < FileResult_Count);
  return g_file_result_strs[result];
}

void file_init() { file_pal_init(); }

FileResult file_write_to_path_sync(const String path, const String data) {
  File*      file = null;
  FileResult res;
  if ((res = file_create(g_alloc_scratch, path, FileMode_Create, FileAccess_Write, &file))) {
    goto ret;
  }
  if ((res = file_write_sync(file, data))) {
    goto ret;
  }
ret:
  if (file) {
    file_destroy(file);
  }
  return res;
}

FileResult file_read_to_end_sync(File* file, DynString* output) {
  FileResult res;
  while ((res = file_read_sync(file, output)) == FileResult_Success)
    ;
  return res == FileResult_NoDataAvailable ? FileResult_Success : res;
}

FileResult file_create_dir_sync(String path) {
  File*      dirHandle;
  FileResult res;

  // Check if the target path already exists; if so: Success.
  res = file_create(g_alloc_scratch, path, FileMode_Open, FileAccess_None, &dirHandle);
  if (res == FileResult_Success) {
    file_destroy(dirHandle);
    return FileResult_Success; // Directory (or other file) exists at the target path; success.
  }

  // Path does not exist yet; First create the parent.
  res = file_create_dir_sync(path_parent(path));
  if (res != FileResult_Success) {
    return res; // Failed to create parent.
  }

  // Create the directory itself.
  return file_pal_create_dir_single_sync(path);
}
