#include "core_alloc.h"
#include "core_array.h"
#include "core_diag.h"
#include "core_dynstring.h"
#include "core_string.h"
#include "net_addr.h"
#include "net_result.h"
#include "net_socket.h"

#include "pal_internal.h"

#include <errno.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

void net_pal_init(void) {}
void net_pal_teardown(void) {}

static const char* to_null_term_scratch(const String str) {
  const Mem scratchMem = alloc_alloc(g_allocScratch, str.size + 1, 1);
  mem_cpy(scratchMem, str);
  *mem_at_u8(scratchMem, str.size) = '\0';
  return scratchMem.ptr;
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

typedef struct sNetSocket {
  Allocator* alloc;
  NetResult  status;
  int        handle;
} NetSocket;

NetSocket* net_socket_connect_sync(Allocator* alloc, const NetAddr addr) {
  NetSocket* s = alloc_alloc_t(alloc, NetSocket);

  *s = (NetSocket){.alloc = alloc};

  s->handle = socket(net_pal_socket_domain(addr.ip.type), SOCK_STREAM /* TCP */, 0);
  if (s->handle < 0) {
    s->status = net_pal_socket_error(errno);
    return s;
  }
  for (;;) {
    switch (addr.ip.type) {
    case NetIpType_V4: {
      struct sockaddr_in sockAddr = {.sin_family = AF_INET};
      mem_write_be_u16(mem_var(sockAddr.sin_port), addr.port);
      mem_cpy(mem_var(sockAddr.sin_addr), mem_var(addr.ip.v4.data));

      if (connect(s->handle, &sockAddr, sizeof(struct sockaddr_in))) {
        if (errno == EINTR) {
          continue; // Interrupted during connect; retry.
        }
        s->status = net_pal_socket_error(errno);
      }
      return s;
    }
    case NetIpType_V6: {
      struct sockaddr_in6 sockAddr = {.sin6_family = AF_INET6};
      mem_write_be_u16(mem_var(sockAddr.sin6_port), addr.port);
      for (u32 i = 0; i != array_elems(addr.ip.v6.groups); ++i) {
        mem_write_be_u16(mem_var(sockAddr.sin6_addr.s6_addr16[i]), addr.ip.v6.groups[i]);
      }
      if (connect(s->handle, &sockAddr, sizeof(struct sockaddr_in6))) {
        if (errno == EINTR) {
          continue; // Interrupted during connect; retry.
        }
        s->status = net_pal_socket_error(errno);
      }
      return s;
    }
    case NetIpType_Count:
      break;
    }
    diag_crash_msg("Unsupported ip-type");
  }
  UNREACHABLE
}

void net_socket_destroy(NetSocket* s) {
  if (s->status == NetResult_Success) {
    diag_assert(s->handle >= 0);
    const int shutdownRet = shutdown(s->handle, SHUT_RDWR);
    (void)shutdownRet;
    diag_assert_msg(!shutdownRet, "Socket shutdown failed (errno: {})", fmt_int(errno));
  }
  if (s->handle >= 0) {
    const int closeRet = close(s->handle);
    (void)closeRet;
    diag_assert_msg(!closeRet, "Socket close failed (errno: {})", fmt_int(errno));
  }
  alloc_free_t(s->alloc, s);
}

NetResult net_socket_status(const NetSocket* s) { return s->status; }

NetResult net_socket_write_sync(NetSocket* s, const String data) {
  if (s->status != NetResult_Success) {
    return s->status;
  }
  diag_assert(s->handle >= 0);
  for (u8* itr = mem_begin(data); itr != mem_end(data);) {
    const ssize_t res = send(s->handle, itr, mem_end(data) - itr, MSG_NOSIGNAL);
    if (res > 0) {
      itr += res;
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
  diag_assert(s->handle >= 0);

  /**
   * TODO: Consider reserving space in the output DynString and directly receiving into that to
   * avoid the copy. Downside is for small reads we would grow the DynString unnecessarily.
   */

  Mem readBuffer = mem_stack(usize_kibibyte);
  while (true) {
    const ssize_t res = recv(s->handle, readBuffer.ptr, readBuffer.size, 0 /* flags */);
    if (res > 0) {
      dynstring_append(out, mem_slice(readBuffer, 0, res));
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

NetResult net_resolve_sync(const String host, NetIp* out) {
  if (UNLIKELY(string_is_empty(host))) {
    return NetResult_InvalidHost;
  }

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

  NetResult result = NetResult_NoEntry;
  for (struct addrinfo* a = addresses; a; a = a->ai_next) {
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
        mem_consume_be_u16(mem_var(addr->sin6_addr.s6_addr16[i]), &out->v6.groups[i]);
      }

      result = NetResult_Success;
      goto Ret;
    }
    default:
      continue; // Unsupported family.
    }
  }

Ret:
  freeaddrinfo(addresses);
  return result;
}
