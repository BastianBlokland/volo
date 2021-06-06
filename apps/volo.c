#include "core_diag.h"
#include "core_init.h"
#include "core_path.h"

int main() {
  core_init();

  diag_log_err("{}: Hello World\n", fmt_text(path_stem(g_path_executable)));

  core_teardown();
  return 0;
}
