#include "core_alloc.h"
#include "core_diag.h"
#include "core_dynlib.h"
#include "log_logger.h"

#include "pal_internal.h"

#include <Windows.h>
#include <ws2tcpip.h>

typedef struct {
  DynLib* lib;
  // clang-format off
  int  (SYS_DECL* GetAddrInfoW)(const wchar_t* nodeName, const wchar_t* serviceName, const ADDRINFOW* hints, ADDRINFOW** out);
  void (SYS_DECL* FreeAddrInfoW)(ADDRINFOW*);
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

#define WS_LOAD_SYM(_NAME_)                                                                        \
  do {                                                                                             \
    ws->_NAME_ = dynlib_symbol(ws->lib, string_lit(#_NAME_));                                      \
    if (!ws->_NAME_) {                                                                             \
      log_w("WinSock symbol '{}' missing", log_param("sym", fmt_text(string_lit(#_NAME_))));       \
      return false;                                                                                \
    }                                                                                              \
  } while (false)

  WS_LOAD_SYM(GetAddrInfoW);
  WS_LOAD_SYM(FreeAddrInfoW);

#undef WS_LOAD_SYM
  return true;
}

static NetWinSockLib g_netWsLib;
static bool          g_netWsReady;

void net_pal_init(void) {
  diag_assert(!g_netWsReady);
  g_netWsReady = net_ws_init(&g_netWsLib, g_allocPersist);
}

void net_pal_teardown(void) {
  if (g_netWsLib.lib) {
    dynlib_destroy(g_netWsLib.lib);
  }
}
