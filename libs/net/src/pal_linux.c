#include "core_alloc.h"
#include "core_array.h"
#include "core_diag.h"
#include "core_string.h"
#include "net_addr.h"
#include "net_result.h"
#include "net_socket.h"

#include "pal_internal.h"

#include <errno.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/un.h>

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
  }
  diag_crash_msg("Unsupported ip-type");
}

typedef struct sNetSocket {
  Allocator* alloc;
  NetResult  state;
  int        handle;
} NetSocket;

NetSocket* net_socket_connect_sync(Allocator* alloc, const NetAddr addr) {
  NetSocket* s = alloc_alloc_t(alloc, NetSocket);

  *s = (NetSocket){.alloc = alloc};

  s->handle = socket(net_pal_socket_domain(addr.ip.type), SOCK_STREAM /* TCP */, 0);
  if (s->handle < 0) {
    s->state = net_pal_socket_error(errno);
    return s;
  }
  switch (addr.ip.type) {
  case NetIpType_V4: {
    struct sockaddr_in sockAddr = {.sin_family = AF_INET};
    mem_write_be_u16(mem_var(sockAddr.sin_port), addr.port);
    mem_cpy(mem_var(sockAddr.sin_addr), mem_var(addr.ip.v4.data));

    if (connect(s->handle, &sockAddr, sizeof(struct sockaddr_in))) {
      s->state = net_pal_socket_error(errno);
      return s;
    }
  } break;
  case NetIpType_V6: {
    struct sockaddr_in6 sockAddr = {.sin6_family = AF_INET};
    mem_write_be_u16(mem_var(sockAddr.sin6_port), addr.port);
    for (u32 i = 0; i != array_elems(addr.ip.v6.groups); ++i) {
      mem_write_be_u16(mem_var(sockAddr.sin6_addr.s6_addr16[i]), addr.ip.v6.groups[i]);
    }
    if (connect(s->handle, &sockAddr, sizeof(struct sockaddr_in6))) {
      s->state = net_pal_socket_error(errno);
      return s;
    }
  } break;
  default:
    diag_crash_msg("Unsupported ip-type");
  }
  return s;
}

void net_socket_destroy(NetSocket* s) { alloc_free_t(s->alloc, s); }

NetResult net_socket_state(const NetSocket* s) { return s->state; }

NetResult net_socket_write_sync(NetSocket* s, const String data) {
  if (s->state != NetResult_Success) {
    return s->state;
  }
  (void)s;
  (void)data;
  return NetResult_UnknownError;
}

NetResult net_socket_read_sync(NetSocket* s, DynString* out) {
  if (s->state != NetResult_Success) {
    return s->state;
  }
  (void)s;
  (void)out;
  return NetResult_UnknownError;
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
