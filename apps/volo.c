#include "core_diag.h"
#include "core_init.h"

int main() {
  core_init();

  diag_log("Hello world\n");
  return 0;
}
