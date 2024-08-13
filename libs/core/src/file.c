#include "core_alloc.h"
#include "core_array.h"
#include "core_diag.h"
#include "core_file.h"
#include "core_path.h"
#include "core_thread.h"

#include "file_internal.h"
#include "init_internal.h"

static i64 g_fileCount;

static const String g_fileResultStrs[] = {
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

ASSERT(array_elems(g_fileResultStrs) == FileResult_Count, "Incorrect number of FileResult strings");

String file_result_str(const FileResult result) {
  diag_assert(result < FileResult_Count);
  return g_fileResultStrs[result];
}

void file_init(void) { file_pal_init(); }

void file_leak_detect(void) {
  if (UNLIKELY(thread_atomic_load_i64(&g_fileCount) != 0)) {
    diag_crash_msg("file: {} handle(s) leaked", fmt_int(g_fileCount));
  }
}

FileResult file_create(
    Allocator*            alloc,
    const String          path,
    const FileMode        mode,
    const FileAccessFlags access,
    File**                file) {
  const FileResult res = file_pal_create(alloc, path, mode, access, file);
  if (res == FileResult_Success) {
    thread_atomic_add_i64(&g_fileCount, 1);
  }
  return res;
}

FileResult file_temp(Allocator* alloc, File** file) {
  const FileResult res = file_pal_temp(alloc, file);
  if (res == FileResult_Success) {
    thread_atomic_add_i64(&g_fileCount, 1);
  }
  return res;
}

void file_destroy(File* file) {
  if (file->mapping.ptr) {
    file_pal_unmap(file, &file->mapping);
  }

  file_pal_destroy(file);
  if (UNLIKELY(thread_atomic_sub_i64(&g_fileCount, 1) <= 0)) {
    diag_crash_msg("file: Double destroy of File");
  }
}

FileResult file_map(File* file, String* output) {
  diag_assert_msg(!file->mapping.ptr, "File is already mapped");

  const FileResult res = file_pal_map(file, &file->mapping);
  if (res == FileResult_Success) {
    *output = mem_create(file->mapping.ptr, file->mapping.size);
  }
  return res;
}

FileResult file_unmap(File* file) {
  diag_assert_msg(file->mapping.ptr, "File not mapped");

  const FileResult res = file_pal_unmap(file, &file->mapping);
  if (res == FileResult_Success) {
    file->mapping = (FileMapping){0};
  }
  return res;
}

FileResult file_write_to_path_sync(const String path, const String data) {
  File*      file = null;
  FileResult res;
  if ((res = file_create(g_allocScratch, path, FileMode_Create, FileAccess_Write, &file))) {
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

FileInfo file_stat_path_sync(const String path) {
  File* file;
  if (file_create(g_allocScratch, path, FileMode_Open, FileAccess_None, &file)) {
    return (FileInfo){0};
  }
  const FileInfo res = file_stat_sync(file);
  file_destroy(file);
  return res;
}

FileResult file_create_dir_sync(String path) {
  File*      dirHandle;
  FileResult res;

  // Check if the target path already exists; if so: Success.
  res = file_create(g_allocScratch, path, FileMode_Open, FileAccess_None, &dirHandle);
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

u32 file_count(void) { return (u32)thread_atomic_load_i64(&g_fileCount); }
