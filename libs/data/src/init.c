#include "core_diag.h"
#include "core_thread.h"
#include "data_init.h"

static bool g_initalized;

void data_init(void) {
  diag_assert(g_threadTid == g_threadMainTid);

  if (!g_initalized) {
    g_initalized = true;

    // TODO: Init.
  }
}

void data_teardown(void) {
  diag_assert(g_threadTid == g_threadMainTid);

  if (g_initalized) {
    g_initalized = false;

    // TODO: Teardown.
  }
}
