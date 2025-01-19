#include "core_alloc.h"
#include "core_diag.h"
#include "core_dynlib.h"

#include "pal_internal.h"

#include <Windows.h>

typedef struct {
  DynLib* lib;
  // clang-format off
  // TODO: Add needed apis.
  // clang-format on
} NetWinSockLib;

static bool net_ws_init(NetWinSockLib* lib, Allocator* alloc) {
  (void)lib;
  (void)alloc;
  return false;
}

static NetWinSockLib g_netWsLib;
static bool          g_netWsInit;

void net_pal_init(void) {
  diag_assert(!g_netWsInit);
  g_netWsInit = net_ws_init(&g_netWsLib, g_allocPersist);
}

void net_pal_teardown(void) {
  if (g_netWsInit) {
    dynlib_destroy(g_netWsLib.lib);
  }
}
