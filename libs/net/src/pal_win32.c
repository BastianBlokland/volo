#include "core_alloc.h"
#include "core_diag.h"
#include "core_dynlib.h"
#include "core_winutils.h"
#include "log_logger.h"
#include "net_addr.h"

#include "pal_internal.h"

#include <Windows.h>
#include <ws2tcpip.h>

typedef struct {
  DynLib* lib;
  // clang-format off
  int  (SYS_DECL* WSAStartup)(WORD versionRequested, WSADATA* out);
  int  (SYS_DECL* WSACleanup)(void);
  int  (SYS_DECL* WSAGetLastError)(void);
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

#define WS_LOAD_SYM(_NAME_)                                                                        \
  do {                                                                                             \
    ws->_NAME_ = dynlib_symbol(ws->lib, string_lit(#_NAME_));                                      \
    if (!ws->_NAME_) {                                                                             \
      log_w("WinSock symbol '{}' missing", log_param("sym", fmt_text(string_lit(#_NAME_))));       \
      return false;                                                                                \
    }                                                                                              \
  } while (false)

  WS_LOAD_SYM(WSAStartup);
  WS_LOAD_SYM(WSACleanup);
  WS_LOAD_SYM(WSAGetLastError);
  WS_LOAD_SYM(GetAddrInfoW);
  WS_LOAD_SYM(FreeAddrInfoW);

  const DWORD requestedVersion = MAKEWORD(2, 2);
  WSADATA     startupData;
  const int   startupErr = ws->WSAStartup(requestedVersion, &startupData);
  if (startupErr) {
    log_e("WinSock library startup failed", log_param("err", fmt_int(startupErr)));
    ws->WSACleanup();
    return false;
  }
  if (LOBYTE(startupData.wVersion) != 2 || HIBYTE(startupData.wVersion) != 2) {
    log_e("WinSock library unsupported");
    ws->WSACleanup();
    return false;
  }

  log_i(
      "WinSock library loaded",
      log_param("path", fmt_path(dynlib_path(ws->lib))),
      log_param("version-major", fmt_int(LOBYTE(startupData.wVersion))),
      log_param("version-minor", fmt_int(HIBYTE(startupData.wVersion))));

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
  if (g_netWsReady) {
    const int cleanupErr = g_netWsLib.WSACleanup();
    if (cleanupErr) {
      log_e("Failed to cleanup WinSock library", log_param("err", fmt_int(cleanupErr)));
    }
  }
  if (g_netWsLib.lib) {
    dynlib_destroy(g_netWsLib.lib);
  }

  g_netWsLib   = (NetWinSockLib){0};
  g_netWsReady = false;
}

static NetDnsResult net_pal_dns_error(void) {
  if (!g_netWsReady) {
    return NetDnsResult_SystemFailure;
  }
  const int wsaErr = g_netWsLib.WSAGetLastError();
  switch (wsaErr) {
  case WSANOTINITIALISED:
    return NetDnsResult_SystemFailure;
  case WSAEAFNOSUPPORT:
  case WSAESOCKTNOSUPPORT:
    return NetDnsResult_UnsupportedService;
  case WSAHOST_NOT_FOUND:
    return NetDnsResult_HostNotFound;
  case WSATRY_AGAIN:
    return NetDnsResult_TryAgain;
  default:
    return NetDnsResult_UnknownError;
  }
}

NetDnsResult net_dns_resolve_sync(const String host, const NetDnsService srv, NetAddr* out) {
  if (UNLIKELY(!g_netWsReady)) {
    return NetDnsResult_SystemFailure;
  }
  if (UNLIKELY(string_is_empty(host))) {
    return NetDnsResult_InvalidHost;
  }
  const wchar_t* hostStrScratch    = winutils_to_widestr_scratch(host).ptr;
  const wchar_t* serviceStrScratch = winutils_to_widestr_scratch(net_dns_service_name(srv)).ptr;

  const ADDRINFOW hints = {
      .ai_family   = AF_UNSPEC,
      .ai_socktype = SOCK_STREAM,
      .ai_protocol = IPPROTO_TCP,
  };

  ADDRINFOW* addresses = null;
  const int  err = g_netWsLib.GetAddrInfoW(hostStrScratch, serviceStrScratch, &hints, &addresses);
  if (err) {
    return net_pal_dns_error();
  }

  NetDnsResult result = NetDnsResult_NoEntry;
  for (ADDRINFOW* a = addresses; a; a = a->ai_next) {
    if (a->ai_socktype != SOCK_STREAM || a->ai_protocol != IPPROTO_TCP) {
      continue; // Only TCP sockets are supported at the moment.
    }
    switch (a->ai_family) {
    case AF_INET: {
      const struct sockaddr_in* addr = (struct sockaddr_in*)a->ai_addr;

      out->ip.type = NetIpType_V4;
      mem_cpy(mem_var(out->ip.v4.data), mem_var(addr->sin_addr));

      out->port = (u16)addr->sin_port;

      result = NetDnsResult_Success;
      goto Ret;
    }
    case AF_INET6: {
      const struct sockaddr_in6* addr = (struct sockaddr_in6*)a->ai_addr;

      out->ip.type = NetIpType_V6;
      mem_cpy(mem_var(out->ip.v6.data), mem_var(addr->sin6_addr));

      out->port = (u16)addr->sin6_port;

      result = NetDnsResult_Success;
      goto Ret;
    }
    default:
      continue; // Unsupported family.
    }
  }

Ret:
  g_netWsLib.FreeAddrInfoW(addresses);
  return result;
}
