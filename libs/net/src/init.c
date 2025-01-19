#include "core_diag.h"
#include "core_thread.h"
#include "net_init.h"

#include "pal_internal.h"

static bool g_initalized;

void net_init(void) {
  diag_assert(g_threadTid == g_threadMainTid);

  if (!g_initalized) {
    g_initalized = true;

    net_pal_init();
  }
}

void net_teardown(void) {
  diag_assert(g_threadTid == g_threadMainTid);

  if (g_initalized) {
    g_initalized = false;

    net_pal_teardown();
  }
}
