#include "check/spec.h"
#include "core/alloc.h"
#include "core/bits.h"
#include "core/dynstring.h"
#include "core/file.h"
#include "core/path.h"
#include "core/rng.h"
#include "core/time.h"

static void test_file_write_data(DynString* str, usize size) {
  for (usize i = 0; i != size; ++i) {
    dynstring_append_char(str, i % 255);
  }
}

static void test_file_verify_data(CheckTestContext* _testCtx, String input) {
  for (usize i = 0; i != input.size; ++i) {
    check_eq_int(*string_at(input, i), i % 255);
  }
}

spec(file) {

  File*     tmpFile = null;
  DynString buffer  = {0};

  setup() {
    file_temp(g_allocHeap, &tmpFile);
    buffer = dynstring_create(g_allocPage, usize_kibibyte * 4);
  }

  it("can read-back content that was written") {
    check_eq_int(file_write_sync(tmpFile, string_lit("Hello World!")), FileResult_Success);
    check_eq_int(file_seek_sync(tmpFile, 0), FileResult_Success);

    check_eq_int(file_read_sync(tmpFile, &buffer), FileResult_Success);
    check_eq_string(dynstring_view(&buffer), string_lit("Hello World!"));
  }

  it("can read a file to the end") {
    const usize testDataSize = 2345;

    // Write test data to the file.
    test_file_write_data(&buffer, testDataSize);
    check_eq_int(file_write_sync(tmpFile, dynstring_view(&buffer)), FileResult_Success);
    check_eq_int(file_seek_sync(tmpFile, 0), FileResult_Success);

    // Read the file to the end.
    dynstring_clear(&buffer);
    check_eq_int(file_read_to_end_sync(tmpFile, &buffer), FileResult_Success);

    // Verify that all data was retrieved.
    check_eq_int(buffer.size, testDataSize);
    test_file_verify_data(_testCtx, dynstring_view(&buffer));
  }

  it("can skip bytes") {
    check_eq_int(file_write_sync(tmpFile, string_lit("Hello World!")), FileResult_Success);
    check_eq_int(file_seek_sync(tmpFile, 0), FileResult_Success);

    check_eq_int(file_skip_sync(tmpFile, 6), FileResult_Success);

    check_eq_int(file_read_sync(tmpFile, &buffer), FileResult_Success);
    check_eq_string(dynstring_view(&buffer), string_lit("World!"));
  }

  it("can retrieve the file size") {
    check_eq_int(file_stat_sync(tmpFile).size, 0);

    file_write_sync(tmpFile, string_lit("Hello World!"));
    check_eq_int(file_stat_sync(tmpFile).size, 12);
  }

  it("can check the file-type of regular files") {
    check_eq_int(file_stat_sync(tmpFile).type, FileType_Regular);
  }

  it("can check the file-type of directories") {
    File* workingDir = null;
    check_eq_int(
        file_create(g_allocHeap, g_pathWorkingDir, FileMode_Open, FileAccess_None, &workingDir),
        FileResult_Success);

    if (workingDir) {
      check_eq_int(file_stat_sync(workingDir).type, FileType_Directory);
      file_destroy(workingDir);
    }
  }

  it("can retrieve the last access and last modification times") {
    const FileInfo info = file_stat_sync(tmpFile);
    check(time_real_duration(info.accessTime, time_real_clock()) < time_minute);
    check(time_real_duration(info.modTime, time_real_clock()) < time_minute);
  }

  it("can query the current position") {
    usize position;
    check_eq_int(file_position_sync(tmpFile, &position), FileResult_Success);
    check_eq_int(position, 0);

    file_write_sync(tmpFile, string_lit("Hello World!"));
    check_eq_int(file_position_sync(tmpFile, &position), FileResult_Success);
    check_eq_int(position, 12);

    check_eq_int(file_seek_sync(tmpFile, 42), FileResult_Success);
    check_eq_int(file_position_sync(tmpFile, &position), FileResult_Success);
    check_eq_int(position, 42);
  }

  it("can read file contents through a memory map") {
    file_write_sync(tmpFile, string_lit("Hello World!"));

    String mapping;
    check_eq_int(file_map(tmpFile, 0, 0, FileHints_None, &mapping), FileResult_Success);
    check_eq_int(mapping.size, 12);
    check_eq_string(mapping, string_lit("Hello World!"));
  }

  it("can initiate a pre-fetch of memory maps") {
    file_write_sync(tmpFile, string_lit("Hello World!"));

    String mapping;
    check_eq_int(file_map(tmpFile, 0, 0, FileHints_Prefetch, &mapping), FileResult_Success);
    check_eq_string(mapping, string_lit("Hello World!"));
  }

  it("can write file contents through a memory map") {
    file_resize_sync(tmpFile, 12);

    String mapping;
    check_eq_int(file_map(tmpFile, 0, 0, FileHints_None, &mapping), FileResult_Success);
    check_eq_int(mapping.size, 12);
    mem_cpy(mapping, string_lit("Hello World!"));

    check_eq_string(mapping, string_lit("Hello World!"));
  }

  it("can unmap files") {
    file_write_sync(tmpFile, string_lit("Hello World!"));

    String mapping1;
    check_eq_int(file_map(tmpFile, 0, 0, FileHints_None, &mapping1), FileResult_Success);
    check_eq_string(mapping1, string_lit("Hello World!"));

    check_eq_int(file_unmap(tmpFile, mem_slice(mapping1, 0, 4)), FileResult_InvalidMapping);
    check_eq_int(file_unmap(tmpFile, mapping1), FileResult_Success);
    check_eq_int(file_unmap(tmpFile, mapping1), FileResult_InvalidMapping);

    String mapping2;
    check_eq_int(file_map(tmpFile, 0, 0, FileHints_None, &mapping2), FileResult_Success);
    check_eq_string(mapping2, string_lit("Hello World!"));

    check_eq_int(file_unmap(tmpFile, string_empty), FileResult_InvalidMapping);
    check_eq_int(file_unmap(tmpFile, mem_var(buffer)), FileResult_InvalidMapping);
  }

  it("can map part of a file") {
    check_eq_int(file_resize_sync(tmpFile, 1024 * 8), FileResult_Success);

    String mapping;
    check_eq_int(file_map(tmpFile, 6, 4, FileHints_None, &mapping), FileResult_Success);
    check_eq_int(mapping.size, 4);
    mem_cpy(mapping, string_lit("Test"));

    check_eq_int(file_unmap(tmpFile, mapping), FileResult_Success);

    check_eq_int(file_map(tmpFile, 1024 * 4, 12, FileHints_None, &mapping), FileResult_Success);
    check_eq_int(mapping.size, 12);

    mem_cpy(mapping, string_lit("Hello World!"));
    check_eq_int(file_unmap(tmpFile, mapping), FileResult_Success);

    check_eq_int(file_map(tmpFile, 0, 0, FileHints_None, &mapping), FileResult_Success);
    check_eq_int(mapping.size, 1024 * 8);
    check(mem_all(string_slice(mapping, 0, 6), 0));
    check_eq_string(string_slice(mapping, 6, 4), string_lit("Test"));
    check(mem_all(string_slice(mapping, 10, 1024 * 4 - 10), 0));
    check_eq_string(string_slice(mapping, 1024 * 4, 12), string_lit("Hello World!"));
  }

  it("can map multiple parts of the file") {
    check_eq_int(file_resize_sync(tmpFile, 16), FileResult_Success);

    String mapping1;
    check_eq_int(file_map(tmpFile, 0, 8, FileHints_None, &mapping1), FileResult_Success);
    check_eq_int(mapping1.size, 8);
    mem_cpy(mapping1, string_lit("Hello!"));

    String mapping2;
    check_eq_int(file_map(tmpFile, 8, 8, FileHints_None, &mapping2), FileResult_Success);
    check_eq_int(mapping2.size, 8);
    mem_cpy(mapping2, string_lit("World!"));

    String mapping3;
    check_eq_int(file_map(tmpFile, 0, 0, FileHints_None, &mapping3), FileResult_Success);
    check_eq_int(mapping3.size, 16);
    check_eq_string(mapping3, string_lit("Hello!\0\0World!\0\0"));
  }

  it("fails if attempting to map at an invalid offset") {
    String mapping;
    check_eq_int(file_map(tmpFile, 42, 0, FileHints_None, &mapping), FileResult_InvalidMapping);
    check_eq_int(file_map(tmpFile, 0, 0, FileHints_None, &mapping), FileResult_FileEmpty);
  }

  it("can check if a file exists") {
    const String existingPath    = g_pathExecutable;
    const String nonExistingPath = string_lit("path_to_non_existent_file_42");
    File*        file            = null;

    // Check through the 'file_stat_path_sync' api.
    check(file_stat_path_sync(existingPath).type == FileType_Regular);
    check(file_stat_path_sync(nonExistingPath).type == FileType_None);

    // Check through making a file handle.
    check_eq_int(
        file_create(g_allocHeap, existingPath, FileMode_Open, FileAccess_None, &file),
        FileResult_Success);
    check(file != null);
    file_destroy(file);
    file = null;

    check_eq_int(
        file_create(g_allocHeap, nonExistingPath, FileMode_Open, FileAccess_None, &file),
        FileResult_NotFound);
    check(file == null);
  }

  it("can read its own executable") {
    File* ownExecutable = null;
    check_eq_int(
        file_create(g_allocHeap, g_pathExecutable, FileMode_Open, FileAccess_Read, &ownExecutable),
        FileResult_Success);
    check(ownExecutable != null);
    check_eq_int(file_stat_sync(ownExecutable).type, FileType_Regular);

    check_eq_int(file_read_sync(ownExecutable, &buffer), FileResult_Success);
    check(buffer.size > 0);

    if (ownExecutable) {
      file_destroy(ownExecutable);
    }
  }

  it("can create a new file by opening a file-handle with 'Create' mode") {
    const String path = path_build_scratch(
        g_pathTempDir, path_name_random_scratch(g_rng, string_lit("volo"), string_empty));

    // Create a new file containing 'Hello World'.
    File* file;
    check_eq_int(
        file_create(g_allocHeap, path, FileMode_Create, FileAccess_Write, &file),
        FileResult_Success);
    check_eq_int(file_write_sync(file, string_lit("Hello World!")), FileResult_Success);
    file_destroy(file);

    // Open the new file and read its content.
    check_eq_int(
        file_create(g_allocHeap, path, FileMode_Open, FileAccess_Read, &file), FileResult_Success);
    check_eq_int(file_read_sync(file, &buffer), FileResult_Success);
    check_eq_string(dynstring_view(&buffer), string_lit("Hello World!"));
    file_destroy(file);

    // Destroy the file.
    file_delete_sync(path);
  }

  it("can create a new directory") {
    const String path = path_build_scratch(
        g_pathTempDir, path_name_random_scratch(g_rng, string_lit("volo"), string_empty));

    check_eq_int(file_create_dir_sync(path), FileResult_Success);

    // Verify that the directory exists.
    File* dirHandle;
    check_eq_int(
        file_create(g_allocScratch, path, FileMode_Open, FileAccess_None, &dirHandle),
        FileResult_Success);
    if (dirHandle) {
      file_destroy(dirHandle);
    }

    check_eq_int(file_delete_dir_sync(path), FileResult_Success);
  }

  it("can move a file") {
    const String pathA = path_build_scratch(
        g_pathTempDir, path_name_random_scratch(g_rng, string_lit("volo"), string_empty));
    const String pathB = path_build_scratch(
        g_pathTempDir, path_name_random_scratch(g_rng, string_lit("volo"), string_empty));

    // Write a new file at location A.
    check_eq_int(file_write_to_path_sync(pathA, string_lit("Hello World!")), FileResult_Success);

    // Verify that no file exists at location B.
    check_eq_int(file_stat_path_sync(pathB).type, FileType_None);

    // Move the file to location B.
    check_eq_int(file_rename(pathA, pathB), FileResult_Success);

    // Verify that the file now exists at location B.
    check_eq_int(file_stat_path_sync(pathB).type, FileType_Regular);

    // Cleanup the file.
    check_eq_int(file_delete_sync(pathB), FileResult_Success);
  }

  it("can overwrite part of a file") {
    check_eq_int(file_write_sync(tmpFile, string_lit("Hello World!")), FileResult_Success);
    check_eq_int(file_seek_sync(tmpFile, 6), FileResult_Success);
    check_eq_int(file_write_sync(tmpFile, string_lit("  Bye")), FileResult_Success);

    check_eq_int(file_seek_sync(tmpFile, 0), FileResult_Success);
    String mapping;
    check_eq_int(file_map(tmpFile, 0, 0, FileHints_None, &mapping), FileResult_Success);
    check_eq_string(mapping, string_lit("Hello   Bye!"));
  }

  it("can rewrite a file") {
    check_eq_int(file_write_sync(tmpFile, string_lit("Test")), FileResult_Success);
    check_eq_int(file_resize_sync(tmpFile, 0), FileResult_Success);

    check_eq_int(file_write_sync(tmpFile, string_lit("Hello World!")), FileResult_Success);

    check_eq_int(file_seek_sync(tmpFile, 0), FileResult_Success);
    String mapping;
    check_eq_int(file_map(tmpFile, 0, 0, FileHints_None, &mapping), FileResult_Success);
    check_eq_string(mapping, string_lit("Hello World!"));
  }

  it("can be cleared using resize and seek") {
    check_eq_int(file_write_sync(tmpFile, string_lit("Test")), FileResult_Success);
    check_eq_int(file_resize_sync(tmpFile, 0), FileResult_Success);
    check_eq_int(file_seek_sync(tmpFile, 4), FileResult_Success);

    check_eq_int(file_write_sync(tmpFile, string_lit("Hello World!")), FileResult_Success);

    check_eq_int(file_seek_sync(tmpFile, 0), FileResult_Success);
    String mapping;
    check_eq_int(file_map(tmpFile, 0, 0, FileHints_None, &mapping), FileResult_Success);
    check_eq_string(mapping, string_lit("\0\0\0\0Hello World!"));
  }

  it("can compute the checksum of a file") {
    const String content = string_lit("Hello World!");

    check_eq_int(file_write_sync(tmpFile, content), FileResult_Success);
    check_eq_int(file_seek_sync(tmpFile, 0), FileResult_Success);

    u32 crc;
    check_eq_int(file_crc_32_sync(tmpFile, &crc), FileResult_Success);
    check_eq_int(crc, bits_crc_32(0, content));

    String mapping;
    check_eq_int(file_map(tmpFile, 0, 0, FileHints_None, &mapping), FileResult_Success);
    check_eq_int(crc, bits_crc_32(0, mapping));
  }

  teardown() {
    file_destroy(tmpFile);
    dynstring_destroy(&buffer);
  }
}
