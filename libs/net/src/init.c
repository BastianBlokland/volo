#include "core/diag.h"
#include "core/thread.h"
#include "net/init.h"

#include "pal.h"
#include "tls.h"

static bool g_initalized;

void net_init(void) {
  diag_assert(g_threadTid == g_threadMainTid);

  if (!g_initalized) {
    g_initalized = true;

    net_pal_init();
    net_tls_init();
  }
}

void net_teardown(void) {
  diag_assert(g_threadTid == g_threadMainTid);

  if (g_initalized) {
    g_initalized = false;

    net_pal_teardown();
    net_tls_teardown();
  }
}
