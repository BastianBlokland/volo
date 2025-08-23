#include "core/diag.h"
#include "core/thread.h"
#include "data/init.h"

#include "registry.h"

static bool g_initalized;

void data_init(void) {
  diag_assert(g_threadTid == g_threadMainTid);

  if (!g_initalized) {
    g_initalized = true;

    data_reg_global_init();
  }
}

void data_teardown(void) {
  diag_assert(g_threadTid == g_threadMainTid);

  if (g_initalized) {
    g_initalized = false;

    data_reg_global_teardown();
  }
}
