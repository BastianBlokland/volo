#include "core_alloc.h"
#include "core_diag.h"
#include "core_dynlib.h"
#include "log_logger.h"

#include "pal_internal.h"

#include <Windows.h>

typedef struct {
  DynLib* lib;
  // clang-format off
  // TODO: Add needed apis.
  // clang-format on
} NetWinSockLib;

static bool net_ws_init(NetWinSockLib* ws, Allocator* alloc) {
  const DynLibResult loadRes = dynlib_load(alloc, string_lit("Ws2_32.dll"), &ws->lib);
  if (UNLIKELY(loadRes != DynLibResult_Success)) {
    const String err = dynlib_result_str(loadRes);
    log_w("Failed to load WinSock library ('Ws2_32.dll')", log_param("err", fmt_text(err)));
    return false;
  }
  log_i("WinSock library loaded", log_param("path", fmt_path(dynlib_path(ws->lib))));

  (void)ws;
  (void)alloc;
  return true;
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
