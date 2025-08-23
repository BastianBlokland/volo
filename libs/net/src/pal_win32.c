#include "core/alloc.h"
#include "core/array.h"
#include "core/diag.h"
#include "core/dynlib.h"
#include "core/dynstring.h"
#include "core/thread.h"
#include "core/winutils.h"
#include "log/logger.h"
#include "net/addr.h"
#include "net/result.h"
#include "net/types.h"

#include "pal.h"

#include <Windows.h>
#include <ws2tcpip.h>
// NOTE: IpHelper needs to be included after the WinSock header.
#include <iphlpapi.h>

typedef struct {
  DynLib* lib;
  bool    ready;
  // clang-format off
  int    (SYS_DECL* WSAStartup)(WORD versionRequested, WSADATA* out);
  int    (SYS_DECL* WSACleanup)(void);
  int    (SYS_DECL* WSAGetLastError)(void);
  SOCKET (SYS_DECL* socket)(int af, int type, int protocol);
  int    (SYS_DECL* closesocket)(SOCKET);
  int    (SYS_DECL* setsockopt)(SOCKET, int level, int optName, const char* optVal, int optLen);
  int    (SYS_DECL* connect)(SOCKET, const void* addr, int addrLen);
  int    (SYS_DECL* send)(SOCKET, const void* buf, int len, int flags);
  int    (SYS_DECL* recv)(SOCKET, void* buf, int len, int flags);
  int    (SYS_DECL* shutdown)(SOCKET, int how);
  int    (SYS_DECL* GetAddrInfoW)(const wchar_t* nodeName, const wchar_t* serviceName, const ADDRINFOW* hints, ADDRINFOW** out);
  void   (SYS_DECL* FreeAddrInfoW)(ADDRINFOW*);
  // clang-format on
} NetWinSock;

typedef struct {
  DynLib* lib;
  bool    ready;
  // clang-format off
  ULONG  (SYS_DECL* GetAdaptersAddresses)(ULONG family, ULONG flags, void* reserved, IP_ADAPTER_ADDRESSES* adapterAddresses, ULONG* sizePointer);
  // clang-format on
} NetIpHelper;

static bool net_ws_init(NetWinSock* ws, Allocator* alloc) {
  diag_assert(!ws->ready);

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
  WS_LOAD_SYM(setsockopt);
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
  ws->ready = true;
  return true;
}

static bool net_iphlp_init(NetIpHelper* ipHlp, Allocator* alloc) {
  diag_assert(!ipHlp->ready);

  const DynLibResult loadRes = dynlib_load(alloc, string_lit("Iphlpapi.dll"), &ipHlp->lib);
  if (UNLIKELY(loadRes != DynLibResult_Success)) {
    const String err = dynlib_result_str(loadRes);
    log_w("Failed to load the IpHelper library ('Iphlpapi.dll')", log_param("err", fmt_text(err)));
    return false;
  }

#define IP_HELPER_LOAD_SYM(_NAME_)                                                                 \
  do {                                                                                             \
    ipHlp->_NAME_ = dynlib_symbol(ipHlp->lib, string_lit(#_NAME_));                                \
    if (!ipHlp->_NAME_) {                                                                          \
      log_w("IpHelper symbol '{}' missing", log_param("sym", fmt_text(string_lit(#_NAME_))));      \
      return false;                                                                                \
    }                                                                                              \
  } while (false)

  IP_HELPER_LOAD_SYM(GetAdaptersAddresses);

#undef IP_HELPER_LOAD_SYM
  ipHlp->ready = true;
  return true;
}

static NetWinSock  g_netWsLib;
static NetIpHelper g_netIpHlpLib;
static bool        g_netInitialized;
static i64         g_netTotalResolves, g_netTotalConnects;
static i64         g_netTotalBytesRead, g_netTotalBytesWrite;

void net_pal_init(void) {
  net_ws_init(&g_netWsLib, g_allocPersist);
  net_iphlp_init(&g_netIpHlpLib, g_allocPersist);
  g_netInitialized = true;
}

void net_pal_teardown(void) {
  if (g_netWsLib.ready) {
    const int cleanupErr = g_netWsLib.WSACleanup();
    if (cleanupErr) {
      log_e("Failed to cleanup WinSock library", log_param("err", fmt_int(cleanupErr)));
    }
  }
  if (g_netWsLib.lib) {
    dynlib_destroy(g_netWsLib.lib);
  }
  if (g_netIpHlpLib.lib) {
    dynlib_destroy(g_netIpHlpLib.lib);
  }
  g_netWsLib       = (NetWinSock){0};
  g_netIpHlpLib    = (NetIpHelper){0};
  g_netInitialized = false;
}

u64 net_pal_total_resolves(void) { return (u64)thread_atomic_load_i64(&g_netTotalResolves); }
u64 net_pal_total_connects(void) { return (u64)thread_atomic_load_i64(&g_netTotalConnects); }
u64 net_pal_total_bytes_read(void) { return (u64)thread_atomic_load_i64(&g_netTotalBytesRead); }
u64 net_pal_total_bytes_write(void) { return (u64)thread_atomic_load_i64(&g_netTotalBytesWrite); }

static int net_pal_socket_domain(const NetAddrType addrType) {
  switch (addrType) {
  case NetAddrType_V4:
    return AF_INET;
  case NetAddrType_V6:
    return AF_INET6;
  case NetAddrType_Count:
    break;
  }
  diag_crash_msg("Unsupported ip-type");
}

static NetResult net_pal_socket_error(void) {
  if (!g_netWsLib.ready) {
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
  if (!g_netWsLib.ready) {
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
  Allocator*  alloc;
  NetResult   status;
  SOCKET      handle;
  NetEndpoint remoteEndpoint;
  NetDir      closedMask;
} NetSocket;

static bool net_socket_configure(NetSocket* s) {
  // clang-format off

  int optValTrue = true;
  if (g_netWsLib.setsockopt(s->handle, IPPROTO_TCP, TCP_NODELAY, (char*)&optValTrue, sizeof(optValTrue)) == SOCKET_ERROR) {
    return false;
  }

  // clang-format on
  return true;
}

NetSocket* net_socket_connect_sync(Allocator* alloc, const NetEndpoint endpoint) {
  if (UNLIKELY(!g_netInitialized)) {
    diag_crash_msg("Network subsystem not initialized");
  }
  NetSocket* s = alloc_alloc_t(alloc, NetSocket);

  *s = (NetSocket){.alloc = alloc, .handle = INVALID_SOCKET, .remoteEndpoint = endpoint};
  if (UNLIKELY(!g_netWsLib.ready)) {
    s->status = NetResult_SystemFailure;
    return s;
  }

  thread_atomic_add_i64(&g_netTotalConnects, 1);

  const int domain = net_pal_socket_domain(endpoint.addr.type);
  s->handle        = g_netWsLib.socket(domain, SOCK_STREAM, IPPROTO_TCP);
  if (s->handle == INVALID_SOCKET) {
    s->status = net_pal_socket_error();
    return s;
  }
  if (!net_socket_configure(s)) {
    s->status = NetResult_SystemFailure;
    return s;
  }

  switch (endpoint.addr.type) {
  case NetAddrType_V4: {
    struct sockaddr_in sockAddr = {.sin_family = AF_INET};
    mem_write_be_u16(mem_var(sockAddr.sin_port), endpoint.port);
    mem_cpy(mem_var(sockAddr.sin_addr), mem_var(endpoint.addr.v4.data));

    if (g_netWsLib.connect(s->handle, &sockAddr, sizeof(struct sockaddr_in)) == SOCKET_ERROR) {
      s->status = net_pal_socket_error();
    }
    return s;
  }
  case NetAddrType_V6: {
    struct sockaddr_in6 sockAddr = {.sin6_family = AF_INET6};
    mem_write_be_u16(mem_var(sockAddr.sin6_port), endpoint.port);
    for (u32 i = 0; i != array_elems(endpoint.addr.v6.groups); ++i) {
      mem_write_be_u16(mem_var(sockAddr.sin6_addr.u.Word[i]), endpoint.addr.v6.groups[i]);
    }
    if (g_netWsLib.connect(s->handle, &sockAddr, sizeof(struct sockaddr_in6)) == SOCKET_ERROR) {
      s->status = net_pal_socket_error();
    }
    return s;
  }
  case NetAddrType_Count:
    break;
  }
  diag_crash_msg("Unsupported ip-type");
}

void net_socket_destroy(NetSocket* s) {
  if (g_netWsLib.ready && s->handle != INVALID_SOCKET) {
    const int closeRet = g_netWsLib.closesocket(s->handle);
    (void)closeRet;
    diag_assert_msg(
        !closeRet,
        "Socket close failed (WSAGetLastError: {})",
        fmt_int(g_netWsLib.WSAGetLastError()));
  }
  alloc_free_t(s->alloc, s);
}

NetResult net_socket_status(const NetSocket* s) {
  if ((s->closedMask & NetDir_Both) == NetDir_Both) {
    return NetResult_ConnectionClosed;
  }
  return s->status;
}

const NetEndpoint* net_socket_remote(const NetSocket* s) { return &s->remoteEndpoint; }

NetResult net_socket_write_sync(NetSocket* s, const String data) {
  if (s->status != NetResult_Success) {
    return s->status;
  }
  if (data.size > i32_max) {
    return NetResult_TooMuchData;
  }
  if (s->closedMask & NetDir_Out) {
    return NetResult_ConnectionClosed;
  }
  diag_assert(s->handle != INVALID_SOCKET);
  for (u8* itr = mem_begin(data); itr != mem_end(data);) {
    const int res = g_netWsLib.send(s->handle, itr, (int)(mem_end(data) - itr), 0 /* flags */);
    if (res > 0) {
      itr += res;
      thread_atomic_add_i64(&g_netTotalBytesWrite, res);
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
  if (s->closedMask & NetDir_In) {
    return NetResult_ConnectionClosed;
  }
  diag_assert(s->handle != INVALID_SOCKET);

  /**
   * TODO: Consider reserving space in the output DynString and directly receiving into that to
   * avoid the copy. Downside is for small reads we would grow the DynString unnecessarily.
   */

  Mem       readBuffer = mem_stack(usize_kibibyte * 16);
  const int res = g_netWsLib.recv(s->handle, readBuffer.ptr, (int)readBuffer.size, 0 /* flags */);
  if (res > 0) {
    dynstring_append(out, mem_slice(readBuffer, 0, res));
    thread_atomic_add_i64(&g_netTotalBytesRead, res);
    return NetResult_Success;
  }
  if (res == 0) {
    return s->status = NetResult_ConnectionClosed;
  }
  return s->status = net_pal_socket_error();
}

NetResult net_socket_shutdown(NetSocket* s, const NetDir dir) {
  if ((s->closedMask & dir) == dir) {
    return NetResult_Success; // Already closed.
  }
  if (s->handle != INVALID_SOCKET) {
    return NetResult_Success; // Socket was never opened.
  }

  int how;
  if ((dir & NetDir_Both) == NetDir_Both) {
    how = SD_BOTH;
  } else if (dir & NetDir_In) {
    how = SD_RECEIVE;
  } else {
    how = SD_SEND;
  }

  if (g_netWsLib.shutdown(s->handle, how)) {
    return s->status = net_pal_socket_error();
  }

  s->closedMask |= dir;
  return NetResult_Success;
}

NetResult net_interfaces(NetAddr out[], u32* count, const NetInterfaceQueryFlags flags) {
  if (UNLIKELY(!g_netInitialized)) {
    diag_crash_msg("Network subsystem not initialized");
  }

  const u32 countMax = *count;
  *count             = 0;

  if (UNLIKELY(!g_netIpHlpLib.ready)) {
    return NetResult_Unsupported; // IpHelper library not available.
  }

  ULONG       memSize    = 16 * usize_kibibyte;
  const Mem   scratchMem = alloc_alloc(g_allocScratch, memSize, alignof(void*));
  const ULONG apiFlags = GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER;
  const ULONG ret =
      g_netIpHlpLib.GetAdaptersAddresses(AF_UNSPEC, apiFlags, NULL, scratchMem.ptr, &memSize);

  if (ret == ERROR_NO_DATA || ret == ERROR_ADDRESS_NOT_ASSOCIATED) {
    return NetResult_Success;
  }
  if (ret == ERROR_BUFFER_OVERFLOW) {
    // TODO: Retry with a larger buffer.
  } else if (ret != ERROR_SUCCESS) {
    diag_crash_msg("GetAdaptersAddresses: {}", fmt_text(winutils_error_msg_scratch(ret)));
  }

  for (IP_ADAPTER_ADDRESSES* adapter = scratchMem.ptr; adapter; adapter = adapter->Next) {
    if (adapter->IfType == IF_TYPE_SOFTWARE_LOOPBACK) {
      continue; // Skip loopback.
    }
    if (adapter->OperStatus != IfOperStatusUp) {
      continue; // Interface not running.
    }
    // Report all unicast addresses.
    for (IP_ADAPTER_UNICAST_ADDRESS* uni = adapter->FirstUnicastAddress; uni; uni = uni->Next) {
      switch (uni->Address.lpSockaddr->sa_family) {
      case AF_INET: {
        const struct sockaddr_in* addr = (struct sockaddr_in*)uni->Address.lpSockaddr;

        NetAddr netAddr;
        netAddr.type = NetAddrType_V4;
        mem_cpy(mem_var(netAddr.v4.data), mem_var(addr->sin_addr));

        if (!(flags & NetInterfaceQueryFlags_IncludeLinkLocal) && net_is_linklocal(netAddr)) {
          continue;
        }
        if (UNLIKELY(*count == countMax)) {
          goto Ret;
        }
        out[(*count)++] = netAddr;
        continue;
      }
      case AF_INET6: {
        const struct sockaddr_in6* addr = (struct sockaddr_in6*)uni->Address.lpSockaddr;

        NetAddr netAddr;
        netAddr.type = NetAddrType_V6;
        for (u32 i = 0; i != array_elems(netAddr.v6.groups); ++i) {
          mem_consume_be_u16(mem_var(addr->sin6_addr.u.Word[i]), &netAddr.v6.groups[i]);
        }

        if (!(flags & NetInterfaceQueryFlags_IncludeLinkLocal) && net_is_linklocal(netAddr)) {
          continue;
        }
        if (UNLIKELY(*count == countMax)) {
          goto Ret;
        }
        out[(*count)++] = netAddr;
        continue;
      }
      }
    }
  }
Ret:
  return NetResult_Success;
}

NetResult net_resolve_sync(const String host, NetAddr out[], u32* count) {
  if (UNLIKELY(!g_netInitialized)) {
    diag_crash_msg("Network subsystem not initialized");
  }
  const u32 countMax = *count;
  *count             = 0;

  if (UNLIKELY(!g_netWsLib.ready)) {
    return NetResult_SystemFailure;
  }
  if (UNLIKELY(string_is_empty(host))) {
    return NetResult_InvalidHost;
  }
  const wchar_t* hostStrScratch = winutils_to_widestr_scratch(host).ptr;

  thread_atomic_add_i64(&g_netTotalResolves, 1);

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

  for (ADDRINFOW* a = addresses; a; a = a->ai_next) {
    if (a->ai_socktype != SOCK_STREAM || a->ai_protocol != IPPROTO_TCP) {
      continue; // Only TCP sockets are supported at the moment.
    }
    switch (a->ai_family) {
    case AF_INET: {
      const struct sockaddr_in* addr = (struct sockaddr_in*)a->ai_addr;

      if (UNLIKELY(*count == countMax)) {
        goto Ret;
      }

      NetAddr* netAddr = &out[(*count)++];
      netAddr->type    = NetAddrType_V4;
      mem_cpy(mem_var(netAddr->v4.data), mem_var(addr->sin_addr));
      continue;
    }
    case AF_INET6: {
      const struct sockaddr_in6* addr = (struct sockaddr_in6*)a->ai_addr;

      if (UNLIKELY(*count == countMax)) {
        goto Ret;
      }

      NetAddr* netAddr = &out[(*count)++];
      netAddr->type    = NetAddrType_V6;
      for (u32 i = 0; i != array_elems(netAddr->v6.groups); ++i) {
        mem_consume_be_u16(mem_var(addr->sin6_addr.u.Word[i]), &netAddr->v6.groups[i]);
      }
      continue;
    }
    default:
      continue; // Unsupported family.
    }
  }

Ret:
  g_netWsLib.FreeAddrInfoW(addresses);
  return *count ? NetResult_Success : NetResult_NoEntry;
}
