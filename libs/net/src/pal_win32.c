#include "core_alloc.h"
#include "core_array.h"
#include "core_diag.h"
#include "core_dynlib.h"
#include "core_winutils.h"
#include "log_logger.h"
#include "net_addr.h"
#include "net_result.h"

#include "pal_internal.h"

#include <Windows.h>
#include <ws2tcpip.h>

typedef struct {
  DynLib* lib;
  // clang-format off
  int    (SYS_DECL* WSAStartup)(WORD versionRequested, WSADATA* out);
  int    (SYS_DECL* WSACleanup)(void);
  int    (SYS_DECL* WSAGetLastError)(void);
  SOCKET (SYS_DECL* socket)(int af, int type, int protocol);
  int    (SYS_DECL* closesocket)(SOCKET);
  int    (SYS_DECL* connect)(SOCKET, const void* addr, int addrLen);
  int    (SYS_DECL* send)(SOCKET, const void* buf, int len, int flags);
  int    (SYS_DECL* recv)(SOCKET, void* buf, int len, int flags);
  int    (SYS_DECL* shutdown)(SOCKET, int how);
  int    (SYS_DECL* GetAddrInfoW)(const wchar_t* nodeName, const wchar_t* serviceName, const ADDRINFOW* hints, ADDRINFOW** out);
  void   (SYS_DECL* FreeAddrInfoW)(ADDRINFOW*);
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
  WS_LOAD_SYM(socket);
  WS_LOAD_SYM(closesocket);
  WS_LOAD_SYM(connect);
  WS_LOAD_SYM(send);
  WS_LOAD_SYM(recv);
  WS_LOAD_SYM(shutdown);
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

static int net_pal_socket_domain(const NetIpType ipType) {
  switch (ipType) {
  case NetIpType_V4:
    return AF_INET;
  case NetIpType_V6:
    return AF_INET6;
  case NetIpType_Count:
    break;
  }
  diag_crash_msg("Unsupported ip-type");
}

static NetResult net_pal_socket_error(void) {
  if (!g_netWsReady) {
    return NetResult_SystemFailure;
  }
  const int wsaErr = g_netWsLib.WSAGetLastError();
  switch (wsaErr) {
  case WSANOTINITIALISED:
  case WSAENETDOWN:
  case WSAEPROVIDERFAILEDINIT:
  case WSAENOBUFS:
    return NetResult_SystemFailure;
  case WSAEAFNOSUPPORT:
  case WSAEPROTONOSUPPORT:
  case WSAESOCKTNOSUPPORT:
    return NetResult_Unsupported;
  case WSAECONNREFUSED:
    return NetResult_Refused;
  case WSAENETRESET:
  case WSAECONNABORTED:
  case WSAECONNRESET:
    return NetResult_ConnectionLost;
  case WSAESHUTDOWN:
    return NetResult_ConnectionClosed;
  case WSAENETUNREACH:
  case WSAEHOSTUNREACH:
  case WSAETIMEDOUT:
    return NetResult_Unreachable;
  default:
    return NetResult_UnknownError;
  }
}

static NetResult net_pal_resolve_error(void) {
  if (!g_netWsReady) {
    return NetResult_SystemFailure;
  }
  const int wsaErr = g_netWsLib.WSAGetLastError();
  switch (wsaErr) {
  case WSANOTINITIALISED:
    return NetResult_SystemFailure;
  case WSAEAFNOSUPPORT:
  case WSAESOCKTNOSUPPORT:
    return NetResult_Unsupported;
  case WSAHOST_NOT_FOUND:
    return NetResult_HostNotFound;
  case WSATRY_AGAIN:
    return NetResult_TryAgain;
  default:
    return NetResult_UnknownError;
  }
}

typedef struct sNetSocket {
  Allocator* alloc;
  NetResult  status;
  SOCKET     handle;
} NetSocket;

NetSocket* net_socket_connect_sync(Allocator* alloc, const NetAddr addr) {
  NetSocket* s = alloc_alloc_t(alloc, NetSocket);

  *s = (NetSocket){.alloc = alloc, .handle = INVALID_SOCKET};
  if (UNLIKELY(!g_netWsReady)) {
    s->status = NetResult_SystemFailure;
    return s;
  }

  s->handle = g_netWsLib.socket(net_pal_socket_domain(addr.ip.type), SOCK_STREAM, IPPROTO_TCP);
  if (s->handle == INVALID_SOCKET) {
    s->status = net_pal_socket_error();
    return s;
  }

  switch (addr.ip.type) {
  case NetIpType_V4: {
    struct sockaddr_in sockAddr = {.sin_family = AF_INET};
    mem_write_be_u16(mem_var(sockAddr.sin_port), addr.port);
    mem_cpy(mem_var(sockAddr.sin_addr), mem_var(addr.ip.v4.data));

    if (g_netWsLib.connect(s->handle, &sockAddr, sizeof(struct sockaddr_in)) == SOCKET_ERROR) {
      s->status = net_pal_socket_error();
    }
    return s;
  }
  case NetIpType_V6: {
    struct sockaddr_in6 sockAddr = {.sin6_family = AF_INET6};
    mem_write_be_u16(mem_var(sockAddr.sin6_port), addr.port);
    for (u32 i = 0; i != array_elems(addr.ip.v6.groups); ++i) {
      mem_write_be_u16(mem_var(sockAddr.sin6_addr.u.Word[i]), addr.ip.v6.groups[i]);
    }
    if (g_netWsLib.connect(s->handle, &sockAddr, sizeof(struct sockaddr_in6)) == SOCKET_ERROR) {
      s->status = net_pal_socket_error();
    }
    return s;
  }
  case NetIpType_Count:
    break;
  }
  diag_crash_msg("Unsupported ip-type");
}

void net_socket_destroy(NetSocket* s) {
  if (g_netWsReady && s->status == NetResult_Success) {
    diag_assert(s->handle != INVALID_SOCKET);
    const int shutdownRet = g_netWsLib.shutdown(s->handle, SD_BOTH);
    (void)shutdownRet;
    diag_assert_msg(
        !shutdownRet,
        "Socket shutdown failed (WSAGetLastError: {})",
        fmt_int(g_netWsLib.WSAGetLastError()));
  }
  if (g_netWsReady && s->handle != INVALID_SOCKET) {
    const int closeRet = g_netWsLib.closesocket(s->handle);
    (void)closeRet;
    diag_assert_msg(
        !closeRet,
        "Socket close failed (WSAGetLastError: {})",
        fmt_int(g_netWsLib.WSAGetLastError()));
  }
  alloc_free_t(s->alloc, s);
}

NetResult net_socket_status(const NetSocket* s) { return s->status; }

NetResult net_socket_write_sync(NetSocket* s, const String data) {
  if (s->status != NetResult_Success) {
    return s->status;
  }
  if (data.size > i32_max) {
    return NetResult_TooMuchData;
  }
  diag_assert(s->handle != INVALID_SOCKET);
  for (u8* itr = mem_begin(data); itr != mem_end(data);) {
    const int res = g_netWsLib.send(s->handle, itr, (int)(mem_end(data) - itr), 0 /* flags */);
    if (res > 0) {
      itr += res;
      continue;
    }
    return s->status = net_pal_socket_error();
  }
  return NetResult_Success;
}

NetResult net_socket_read_sync(NetSocket* s, DynString* out) {
  if (s->status != NetResult_Success) {
    return s->status;
  }
  (void)out;
  return NetResult_UnknownError;
}

NetResult net_resolve_sync(const String host, NetIp* out) {
  if (UNLIKELY(!g_netWsReady)) {
    return NetResult_SystemFailure;
  }
  if (UNLIKELY(string_is_empty(host))) {
    return NetResult_InvalidHost;
  }
  const wchar_t* hostStrScratch = winutils_to_widestr_scratch(host).ptr;

  const ADDRINFOW hints = {
      .ai_family   = AF_UNSPEC,
      .ai_socktype = SOCK_STREAM,
      .ai_protocol = IPPROTO_TCP,
  };

  ADDRINFOW* addresses = null;
  const int  err       = g_netWsLib.GetAddrInfoW(hostStrScratch, null, &hints, &addresses);
  if (err) {
    return net_pal_resolve_error();
  }

  NetResult result = NetResult_NoEntry;
  for (ADDRINFOW* a = addresses; a; a = a->ai_next) {
    if (a->ai_socktype != SOCK_STREAM || a->ai_protocol != IPPROTO_TCP) {
      continue; // Only TCP sockets are supported at the moment.
    }
    switch (a->ai_family) {
    case AF_INET: {
      const struct sockaddr_in* addr = (struct sockaddr_in*)a->ai_addr;

      out->type = NetIpType_V4;
      mem_cpy(mem_var(out->v4.data), mem_var(addr->sin_addr));

      result = NetResult_Success;
      goto Ret;
    }
    case AF_INET6: {
      const struct sockaddr_in6* addr = (struct sockaddr_in6*)a->ai_addr;

      out->type = NetIpType_V6;
      for (u32 i = 0; i != array_elems(out->v6.groups); ++i) {
        mem_consume_be_u16(mem_var(addr->sin6_addr.u.Word[i]), &out->v6.groups[i]);
      }

      result = NetResult_Success;
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
