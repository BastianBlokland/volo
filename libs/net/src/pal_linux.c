#include "core_alloc.h"
#include "core_string.h"
#include "net_addr.h"
#include "net_dns.h"

#include "pal_internal.h"

#include <netdb.h>
#include <sys/socket.h>
#include <sys/types.h>

void net_pal_init(void) {}
void net_pal_teardown(void) {}

static const char* to_null_term_scratch(const String str) {
  const Mem scratchMem = alloc_alloc(g_allocScratch, str.size + 1, 1);
  mem_cpy(scratchMem, str);
  *mem_at_u8(scratchMem, str.size) = '\0';
  return scratchMem.ptr;
}

static NetDnsResult net_pal_to_dns_error(const int err) {
  switch (err) {
  case EAI_NODATA:
    return NetDnsResult_NoEntry;
  case EAI_SERVICE:
  case EAI_ADDRFAMILY:
  case EAI_SOCKTYPE:
    return NetDnsResult_UnsupportedService;
  case EAI_NONAME:
    return NetDnsResult_HostNotFound;
  case EAI_AGAIN:
    return NetDnsResult_TryAgain;
  case EAI_SYSTEM:
    return NetDnsResult_SystemFailure;
  default:
    return NetDnsResult_UnknownError;
  }
}

NetDnsResult net_dns_resolve_sync(const String host, const NetDnsService srv, NetAddr* out) {
  if (UNLIKELY(string_is_empty(host))) {
    return NetDnsResult_InvalidHost;
  }
  const char* hostNullTerm = to_null_term_scratch(host);
  const char* srvNullTerm  = to_null_term_scratch(net_dns_service_name(srv));

  const struct addrinfo hints = {
      .ai_family   = AF_UNSPEC,
      .ai_socktype = SOCK_STREAM,
      .ai_protocol = IPPROTO_TCP,
  };
  struct addrinfo* addresses = null;
  const int        err       = getaddrinfo(hostNullTerm, srvNullTerm, &hints, &addresses);
  if (err) {
    return net_pal_to_dns_error(err);
  }

  NetDnsResult result = NetDnsResult_NoEntry;
  for (struct addrinfo* a = addresses; a; a = a->ai_next) {
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
  freeaddrinfo(addresses);
  return result;
}
