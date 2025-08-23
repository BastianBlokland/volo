#include "core/alloc.h"
#include "core/array.h"
#include "core/diag.h"
#include "core/dynstring.h"
#include "core/string.h"
#include "core/thread.h"
#include "net/addr.h"
#include "net/result.h"
#include "net/socket.h"
#include "net/types.h"

#include "pal.h"

#include <errno.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netdb.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

static bool g_netInitialized;
static i64  g_netTotalResolves, g_netTotalConnects;
static i64  g_netTotalBytesRead, g_netTotalBytesWrite;

void net_pal_init(void) { g_netInitialized = true; }
void net_pal_teardown(void) { g_netInitialized = false; }

u64 net_pal_total_resolves(void) { return (u64)thread_atomic_load_i64(&g_netTotalResolves); }
u64 net_pal_total_connects(void) { return (u64)thread_atomic_load_i64(&g_netTotalConnects); }
u64 net_pal_total_bytes_read(void) { return (u64)thread_atomic_load_i64(&g_netTotalBytesRead); }
u64 net_pal_total_bytes_write(void) { return (u64)thread_atomic_load_i64(&g_netTotalBytesWrite); }

static const char* to_null_term_scratch(const String str) {
  const Mem scratchMem = alloc_alloc(g_allocScratch, str.size + 1, 1);
  mem_cpy(scratchMem, str);
  *mem_at_u8(scratchMem, str.size) = '\0';
  return scratchMem.ptr;
}

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

static NetResult net_pal_socket_error(const int err) {
  switch (err) {
  case EAFNOSUPPORT:
  case EINVAL:
  case EPROTONOSUPPORT:
    return NetResult_Unsupported;
  case EAGAIN:
    return NetResult_TryAgain;
  case ECONNREFUSED:
    return NetResult_Refused;
  case ENETUNREACH:
  case EHOSTUNREACH:
  case ETIMEDOUT:
    return NetResult_Unreachable;
  case ECONNRESET:
    return NetResult_ConnectionLost;
  case EMFILE:
  case ENFILE:
  case ENOBUFS:
  case ENOMEM:
    return NetResult_SystemFailure;
  default:
    return NetResult_UnknownError;
  }
}

static NetResult net_pal_resolve_error(const int err) {
  switch (err) {
  case EAI_NODATA:
    return NetResult_NoEntry;
  case EAI_SERVICE:
  case EAI_ADDRFAMILY:
  case EAI_SOCKTYPE:
    return NetResult_Unsupported;
  case EAI_NONAME:
    return NetResult_HostNotFound;
  case EAI_AGAIN:
    return NetResult_TryAgain;
  case EAI_SYSTEM:
    return NetResult_SystemFailure;
  default:
    return NetResult_UnknownError;
  }
}

typedef struct sNetSocket {
  Allocator*  alloc;
  NetResult   status;
  int         handle;
  NetEndpoint remoteEndpoint;
  NetDir      closedMask;
} NetSocket;

static bool net_socket_configure(NetSocket* s) {
  int optValTrue = true;
  if (setsockopt(s->handle, IPPROTO_TCP, TCP_NODELAY, (char*)&optValTrue, sizeof(optValTrue)) < 0) {
    return false;
  }
  return true;
}

NetSocket* net_socket_connect_sync(Allocator* alloc, const NetEndpoint endpoint) {
  if (UNLIKELY(!g_netInitialized)) {
    diag_crash_msg("Network subsystem not initialized");
  }
  NetSocket* s = alloc_alloc_t(alloc, NetSocket);

  *s = (NetSocket){.alloc = alloc, .handle = -1, .remoteEndpoint = endpoint};

  thread_atomic_add_i64(&g_netTotalConnects, 1);

  s->handle = socket(net_pal_socket_domain(endpoint.addr.type), SOCK_STREAM, IPPROTO_TCP);
  if (s->handle < 0) {
    s->status = net_pal_socket_error(errno);
    return s;
  }
  if (!net_socket_configure(s)) {
    s->status = NetResult_SystemFailure;
    return s;
  }
  for (;;) {
    switch (endpoint.addr.type) {
    case NetAddrType_V4: {
      struct sockaddr_in sockAddr = {.sin_family = AF_INET};
      mem_write_be_u16(mem_var(sockAddr.sin_port), endpoint.port);
      mem_cpy(mem_var(sockAddr.sin_addr), mem_var(endpoint.addr.v4.data));

      if (connect(s->handle, &sockAddr, sizeof(struct sockaddr_in))) {
        if (errno == EINTR) {
          continue; // Interrupted during connect; retry.
        }
        s->status = net_pal_socket_error(errno);
      }
      return s;
    }
    case NetAddrType_V6: {
      struct sockaddr_in6 sockAddr = {.sin6_family = AF_INET6};
      mem_write_be_u16(mem_var(sockAddr.sin6_port), endpoint.port);
      for (u32 i = 0; i != array_elems(endpoint.addr.v6.groups); ++i) {
        mem_write_be_u16(mem_var(sockAddr.sin6_addr.s6_addr16[i]), endpoint.addr.v6.groups[i]);
      }
      if (connect(s->handle, &sockAddr, sizeof(struct sockaddr_in6))) {
        if (errno == EINTR) {
          continue; // Interrupted during connect; retry.
        }
        s->status = net_pal_socket_error(errno);
      }
      return s;
    }
    case NetAddrType_Count:
      break;
    }
    diag_crash_msg("Unsupported ip-type");
  }
  UNREACHABLE
}

void net_socket_destroy(NetSocket* s) {
  if (s->handle >= 0) {
    const int closeRet = close(s->handle);
    (void)closeRet;
    diag_assert_msg(!closeRet, "Socket close failed (errno: {})", fmt_int(errno));
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
  diag_assert(s->handle >= 0);
  for (u8* itr = mem_begin(data); itr != mem_end(data);) {
    const ssize_t res = send(s->handle, itr, mem_end(data) - itr, MSG_NOSIGNAL);
    if (res > 0) {
      itr += res;
      thread_atomic_add_i64(&g_netTotalBytesWrite, res);
      continue;
    }
    switch (errno) {
    case EINTR:
      continue; // Retry on interrupt.
    }
    return s->status = net_pal_socket_error(errno);
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
  diag_assert(s->handle >= 0);

  /**
   * TODO: Consider reserving space in the output DynString and directly receiving into that to
   * avoid the copy. Downside is for small reads we would grow the DynString unnecessarily.
   */

  Mem readBuffer = mem_stack(usize_kibibyte * 16);
  while (true) {
    const ssize_t res = recv(s->handle, readBuffer.ptr, readBuffer.size, 0 /* flags */);
    if (res > 0) {
      dynstring_append(out, mem_slice(readBuffer, 0, res));
      thread_atomic_add_i64(&g_netTotalBytesRead, res);
      return NetResult_Success;
    }
    if (res == 0) {
      return s->status = NetResult_ConnectionClosed;
    }
    switch (errno) {
    case EINTR:
      continue; // Retry on interrupt.
    }
    return s->status = net_pal_socket_error(errno);
  }
}

NetResult net_socket_shutdown(NetSocket* s, const NetDir dir) {
  if ((s->closedMask & dir) == dir) {
    return NetResult_Success; // Already closed.
  }
  if (s->handle < 0) {
    return NetResult_Success; // Socket was never opened.
  }

  int how;
  if ((dir & NetDir_Both) == NetDir_Both) {
    how = SHUT_RDWR;
  } else if (dir & NetDir_In) {
    how = SHUT_RD;
  } else {
    how = SHUT_WR;
  }

  if (shutdown(s->handle, how) && errno != ENOTCONN) {
    return s->status = net_pal_socket_error(errno);
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

  struct ifaddrs* addrs;
  const int       res = getifaddrs(&addrs);
  if (UNLIKELY(res < 0)) {
    return NetResult_UnknownError; // Failed to lookup addresses.
  }
  for (struct ifaddrs* itr = addrs; itr; itr = itr->ifa_next) {
    if (!itr->ifa_addr) {
      continue; // Interface has no address.
    }
    if (!(itr->ifa_flags & IFF_UP)) {
      continue; // Interface not running.
    }
    if (itr->ifa_flags & IFF_LOOPBACK) {
      continue; // Exclude loop-back interfaces.
    }
    switch (itr->ifa_addr->sa_family) {
    case AF_INET: {
      const struct sockaddr_in* addr = (struct sockaddr_in*)itr->ifa_addr;

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
      const struct sockaddr_in6* addr = (struct sockaddr_in6*)itr->ifa_addr;

      NetAddr netAddr;
      netAddr.type = NetAddrType_V6;
      for (u32 i = 0; i != array_elems(netAddr.v6.groups); ++i) {
        mem_consume_be_u16(mem_var(addr->sin6_addr.s6_addr16[i]), &netAddr.v6.groups[i]);
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

Ret:
  freeifaddrs(addrs);
  return NetResult_Success;
}

NetResult net_resolve_sync(const String host, NetAddr out[], u32* count) {
  if (UNLIKELY(!g_netInitialized)) {
    diag_crash_msg("Network subsystem not initialized");
  }
  const u32 countMax = *count;
  *count             = 0;

  if (UNLIKELY(string_is_empty(host))) {
    return NetResult_InvalidHost;
  }

  thread_atomic_add_i64(&g_netTotalResolves, 1);

  const struct addrinfo hints = {
      .ai_family   = AF_UNSPEC,
      .ai_socktype = SOCK_STREAM,
      .ai_protocol = IPPROTO_TCP,
  };
  struct addrinfo* addresses = null;
  const int        err       = getaddrinfo(to_null_term_scratch(host), null, &hints, &addresses);
  if (err) {
    return net_pal_resolve_error(err);
  }

  for (struct addrinfo* a = addresses; a; a = a->ai_next) {
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
        mem_consume_be_u16(mem_var(addr->sin6_addr.s6_addr16[i]), &netAddr->v6.groups[i]);
      }
      continue;
    }
    default:
      continue; // Unsupported family.
    }
  }

Ret:
  freeaddrinfo(addresses);
  return *count ? NetResult_Success : NetResult_NoEntry;
}
