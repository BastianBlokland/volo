#include "core_alloc.h"
#include "core_array.h"
#include "core_string.h"
#include "net_addr.h"
#include "net_result.h"
#include "net_socket.h"

#include "pal_internal.h"

#include <netdb.h>
#include <sys/socket.h>

void net_pal_init(void) {}
void net_pal_teardown(void) {}

static const char* to_null_term_scratch(const String str) {
  const Mem scratchMem = alloc_alloc(g_allocScratch, str.size + 1, 1);
  mem_cpy(scratchMem, str);
  *mem_at_u8(scratchMem, str.size) = '\0';
  return scratchMem.ptr;
}

static NetResult net_pal_resolve_error(const int err) {
  switch (err) {
  case EAI_NODATA:
    return NetResult_NoEntry;
  case EAI_SERVICE:
  case EAI_ADDRFAMILY:
  case EAI_SOCKTYPE:
    return NetResult_UnsupportedService;
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
  Allocator*     alloc;
  NetSocketState state;
  int            handle;
} NetSocket;

NetSocket* net_socket_create(Allocator* alloc) {
  NetSocket* socket = alloc_alloc_t(alloc, NetSocket);

  *socket = (NetSocket){.alloc = alloc};

  return socket;
}

void net_socket_destroy(NetSocket* socket) { alloc_free_t(socket->alloc, socket); }

NetSocketState net_socket_state(const NetSocket* socket) { return socket->state; }

NetResult net_socket_connect_sync(NetSocket* socket, const NetAddr addr) {
  (void)socket;
  (void)addr;
  return NetResult_UnknownError;
}

NetResult net_socket_write_sync(NetSocket* socket, const String data) {
  (void)socket;
  (void)data;
  return NetResult_UnknownError;
}

NetResult net_socket_read_sync(NetSocket* socket, DynString* out) {
  (void)socket;
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
