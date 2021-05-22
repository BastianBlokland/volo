#include "core_alloc.h"
#include "core_diag.h"

int main() {
  alloc_init();

  diag_log("Hello world\n");
  return 0;
}
