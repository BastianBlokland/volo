#include "core_diag.h"
#include "core_thread.h"
#include "net_init.h"

static bool g_initalized;

void net_init(void) {
  diag_assert(g_threadTid == g_threadMainTid);

  if (!g_initalized) {
    g_initalized = true;

    // TODO: Perform initialization.
  }
}

void net_teardown(void) {
  diag_assert(g_threadTid == g_threadMainTid);

  if (g_initalized) {
    g_initalized = false;

    // TODO: Perform teardown.
  }
}
