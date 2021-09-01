#include "core_file.h"
#include "core_path.h"
#include "core_time.h"

#include "check_spec.h"

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
    file_temp(g_alloc_heap, &tmpFile);
    buffer = dynstring_create(g_alloc_page, usize_kibibyte * 4);
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
        file_create(g_alloc_heap, g_path_workingdir, FileMode_Open, FileAccess_None, &workingDir),
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

  it("can read file contents through a memory map") {
    file_write_sync(tmpFile, string_lit("Hello World!"));

    String mapping;
    check_eq_int(file_map(tmpFile, &mapping), FileResult_Success);
    check_eq_string(mapping, string_lit("Hello World!"));
  }

  it("can write file contents through a memory map") {
    file_write_sync(tmpFile, string_lit("            "));

    String mapping;
    check_eq_int(file_map(tmpFile, &mapping), FileResult_Success);
    mem_cpy(mapping, string_lit("Hello World!"));

    check_eq_string(mapping, string_lit("Hello World!"));
  }

  it("can check if a file exists") {
    File* nonExistingFile = null;
    check_eq_int(
        file_create(
            g_alloc_heap,
            string_lit("path_to_non_existent_file_42"),
            FileMode_Open,
            FileAccess_Read,
            &nonExistingFile),
        FileResult_NotFound);
    check(nonExistingFile == null);
  }

  it("can read its own executable") {
    File* ownExecutable = null;
    check_eq_int(
        file_create(
            g_alloc_heap, g_path_executable, FileMode_Open, FileAccess_Read, &ownExecutable),
        FileResult_Success);
    check(ownExecutable != null);
    check_eq_int(file_stat_sync(ownExecutable).type, FileType_Regular);

    check_eq_int(file_read_sync(ownExecutable, &buffer), FileResult_Success);
    check(buffer.size > 0);

    if (ownExecutable) {
      file_destroy(ownExecutable);
    }
  }

  teardown() {
    file_destroy(tmpFile);
    dynstring_destroy(&buffer);
  }
}
