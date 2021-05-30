#include "core_init.h"
#include "core_types.h"

static bool g_intialized;

void alloc_init();
void time_init();
void file_init();

void core_init() {
  if (!g_intialized) {
    g_intialized = true;

    alloc_init();
    time_init();
    file_init();
  }
}
