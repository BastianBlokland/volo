#include "core_diag.h"
#include "core_file.h"

static void test_file_read_write() {
  Allocator* alloc = alloc_bump_create_stack(256);

  File* file;
  diag_assert(file_temp(alloc, &file) == FileResult_Success);
  diag_assert(file_write_sync(file, string_lit("Hello World!")) == FileResult_Success);
  diag_assert(file_seek_sync(file, 0) == FileResult_Success);

  DynString str = dynstring_create(alloc, 64);

  diag_assert(file_read_sync(file, &str) == FileResult_Success);
  diag_assert(string_eq(dynstring_view(&str), string_lit("Hello World!")));

  dynstring_destroy(&str);
  file_destroy(file);
}

void test_file() { test_file_read_write(); }
