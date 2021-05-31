#include "core_diag.h"
#include "core_file.h"
#include "core_init.h"

int main() {
  core_init();

  file_write_sync(g_file_stdout, string_lit("Hello World\n"));

  core_teardown();
  return 0;
}
