#include "core_alloc.h"
#include "core_diag.h"
#include "core_file.h"
#include "core_init.h"

int main() {
  core_init();

  DynString string = dynstring_create(g_allocator_heap, 1024);

  file_read_sync(g_file_stdin, &string);

  // file_write_sync(g_file_stdout, string_lit("Hello World\n"));
  file_write_sync(g_file_stdout, dynstring_view(&string));

  dynstring_destroy(&string);
  return 0;
}
