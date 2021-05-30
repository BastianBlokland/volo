#include "core_alloc.h"
#include "core_diag.h"
#include "core_file.h"
#include "core_init.h"

int main() {
  core_init();

  File*      f;
  FileResult res;
  if ((res = file_temp(g_allocator_heap, &f))) {
    file_write_sync(g_file_stderr, file_result_str(res));
  }

  file_write_sync(f, string_lit("Hello World\n"));

  file_write_sync(g_file_stdout, string_lit("Hello World\n"));

  DynString str = dynstring_create(g_allocator_heap, 42);
  file_read_sync(g_file_stdin, &str);

  return 0;
}
