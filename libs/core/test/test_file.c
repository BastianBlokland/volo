#include "core_file.h"

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

  File*     file   = null;
  DynString buffer = {0};

  setup() {
    file_temp(g_alloc_heap, &file);
    buffer = dynstring_create(g_alloc_page, usize_kibibyte * 4);
  }

  it("can read-back content that was written") {
    check_eq_int(file_write_sync(file, string_lit("Hello World!")), FileResult_Success);
    check_eq_int(file_seek_sync(file, 0), FileResult_Success);

    check_eq_int(file_read_sync(file, &buffer), FileResult_Success);
    check_eq_string(dynstring_view(&buffer), string_lit("Hello World!"));
  }

  it("can read a file to the end") {
    const usize testDataSize = 2345;

    // Write test data to the file.
    test_file_write_data(&buffer, testDataSize);
    check_eq_int(file_write_sync(file, dynstring_view(&buffer)), FileResult_Success);
    check_eq_int(file_seek_sync(file, 0), FileResult_Success);

    // Read the file to the end.
    dynstring_clear(&buffer);
    check_eq_int(file_read_to_end_sync(file, &buffer), FileResult_Success);

    // Verify that all data was retrieved.
    check_eq_int(buffer.size, testDataSize);
    test_file_verify_data(_testCtx, dynstring_view(&buffer));
  }

  teardown() {
    file_destroy(file);
    dynstring_destroy(&buffer);
  }
}
