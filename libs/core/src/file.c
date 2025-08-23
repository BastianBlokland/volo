#include "core/alloc.h"
#include "core/array.h"
#include "core/diag.h"
#include "core/file.h"
#include "core/path.h"
#include "core/thread.h"

#include "file.h"
#include "init.h"

static i64 g_fileCount, g_fileMappingSize;

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
    string_static("FileTooBig"),
    string_static("InvalidMapping"),
    string_static("FileUnknownError"),
};

ASSERT(array_elems(g_fileResultStrs) == FileResult_Count, "Incorrect number of FileResult strings");

static i8 file_mapping_compare(const void* a, const void* b) {
  const FileMapping* mappingA = a;
  const FileMapping* mappingB = b;
  return compare_uptr(&mappingA->ptr, &mappingB->ptr);
}

static FileMapping* file_mapping_find(File* file, void* ptr) {
  const FileMapping target = {.ptr = ptr};
  return dynarray_search_binary(&file->mappings, file_mapping_compare, &target);
}

static void file_mapping_add(File* file, const FileMapping* mapping) {
  *dynarray_insert_sorted_t(&file->mappings, FileMapping, file_mapping_compare, mapping) = *mapping;
}

static void file_mapping_remove(File* file, const FileMapping* mapping) {
  diag_assert(file->mappings.size);

  const usize index = mapping - dynarray_begin_t(&file->mappings, FileMapping);
  diag_assert(index < file->mappings.size);

  dynarray_remove(&file->mappings, index, 1);
}

String file_result_str(const FileResult result) {
  diag_assert(result < FileResult_Count);
  return g_fileResultStrs[result];
}

void file_init(void) { file_pal_init(); }

void file_leak_detect(void) {
  if (UNLIKELY(thread_atomic_load_i64(&g_fileCount) != 0)) {
    diag_crash_msg("file: {} handle(s) leaked", fmt_int(g_fileCount));
  }
  if (UNLIKELY(thread_atomic_load_i64(&g_fileMappingSize) != 0)) {
    diag_crash_msg("file: mappings leaked (size: {})", fmt_size(g_fileMappingSize));
  }
}

FileResult file_create(
    Allocator*            alloc,
    const String          path,
    const FileMode        mode,
    const FileAccessFlags access,
    File**                file) {
  if (UNLIKELY(string_is_empty(path))) {
    return FileResult_PathInvalid;
  }
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
  dynarray_for_t(&file->mappings, FileMapping, mapping) {
    file_pal_unmap(file, mapping);
    thread_atomic_sub_i64(&g_fileMappingSize, (i64)mapping->size);
  }
  dynarray_clear(&file->mappings);

  file_pal_destroy(file);
  if (UNLIKELY(thread_atomic_sub_i64(&g_fileCount, 1) <= 0)) {
    diag_crash_msg("file: Double destroy of File");
  }
}

FileResult
file_map(File* file, const usize offset, const usize size, const FileHints hints, String* output) {
  if (UNLIKELY(!file->mappings.stride)) {
    return FileResult_InvalidMapping; // File does not support mapping.
  }

  FileMapping      mapping;
  const FileResult res = file_pal_map(file, offset, size, hints, &mapping);
  if (res == FileResult_Success) {
    thread_atomic_add_i64(&g_fileMappingSize, (i64)mapping.size);
    file_mapping_add(file, &mapping);
    *output = mem_create(mapping.ptr, mapping.size);
  }
  return res;
}

FileResult file_unmap(File* file, const String mapping) {
  FileMapping* mappingInfo = file_mapping_find(file, mapping.ptr);
  if (UNLIKELY(!mappingInfo || mappingInfo->size != mapping.size)) {
    return FileResult_InvalidMapping;
  }

  const FileResult res = file_pal_unmap(file, mappingInfo);
  if (res == FileResult_Success) {
    thread_atomic_sub_i64(&g_fileMappingSize, (i64)mappingInfo->size);
    file_mapping_remove(file, mappingInfo);
  }
  return res;
}

FileResult file_write_to_path_sync(const String path, const String data) {
  File*      file = null;
  FileResult res;
  if ((res = file_create(g_allocScratch, path, FileMode_Create, FileAccess_Write, &file))) {
    goto ret;
  }
  if (!string_is_empty(data) && (res = file_write_sync(file, data))) {
    goto ret;
  }
ret:
  if (file) {
    file_destroy(file);
  }
  return res;
}

FileResult file_write_to_path_atomic(const String path, const String data) {
  /**
   * NOTE: Its important to use the same directory as the target for the temporary file as we need
   * to make sure its on the same filesystem (and not on tmpfs for example).
   */
  const String tmpPath = fmt_write_scratch("{}.tmp", fmt_text(path));

  FileResult res;
  if ((res = file_write_to_path_sync(tmpPath, data))) {
    file_delete_sync(tmpPath);
    return res;
  }
  if ((res = file_rename(tmpPath, path))) {
    file_delete_sync(tmpPath);
    return res;
  }
  return FileResult_Success;
}

FileResult file_read_to_end_sync(File* file, DynString* output) {
  FileResult res;
  while ((res = file_read_sync(file, output)) == FileResult_Success)
    ;
  return res == FileResult_NoDataAvailable ? FileResult_Success : res;
}

FileResult file_crc_32_path_sync(const String path, u32* outCrc32) {
  File*      file = null;
  FileResult res;
  if ((res = file_create(g_allocScratch, path, FileMode_Open, FileAccess_Read, &file))) {
    goto ret;
  }
  if ((res = file_crc_32_sync(file, outCrc32))) {
    goto ret;
  }
ret:
  if (file) {
    file_destroy(file);
  }
  return res;
}

FileResult file_create_dir_sync(const String path) {
  if (UNLIKELY(string_is_empty(path))) {
    return FileResult_PathInvalid;
  }
  File*      dirHandle;
  FileResult res;

  // Check if the target path already exists; if so: Success.
  res = file_create(g_allocScratch, path, FileMode_Open, FileAccess_None, &dirHandle);
  if (res == FileResult_Success) {
    file_destroy(dirHandle);
    return FileResult_Success; // Directory (or other file) exists at the target path; success.
  }

  // Path does not exist yet; First create the parent.
  const String parent = path_parent(path);
  if (!string_is_empty(parent)) {
    res = file_create_dir_sync(parent);
    if (res != FileResult_Success) {
      return res; // Failed to create parent.
    }
  }

  // Create the directory itself.
  return file_pal_create_dir_single_sync(path);
}

u32   file_count(void) { return (u32)thread_atomic_load_i64(&g_fileCount); }
usize file_mapping_size(void) { return (usize)thread_atomic_load_i64(&g_fileMappingSize); }
