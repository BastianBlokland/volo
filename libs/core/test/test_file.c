#include "core_file.h"

#include "check_spec.h"

spec(file) {

  it("can read an write file contents") {
    Allocator* alloc = alloc_bump_create_stack(256);

    File* file;
    check_eq_int(file_temp(alloc, &file), FileResult_Success);
    check_eq_int(file_write_sync(file, string_lit("Hello World!")), FileResult_Success);
    check_eq_int(file_seek_sync(file, 0), FileResult_Success);

    DynString str = dynstring_create(alloc, 64);

    check_eq_int(file_read_sync(file, &str), FileResult_Success);
    check_eq_string(dynstring_view(&str), string_lit("Hello World!"));

    dynstring_destroy(&str);
    file_destroy(file);
  }
}
