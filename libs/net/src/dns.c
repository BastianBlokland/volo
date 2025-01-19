#include "core_string.h"
#include "net_dns.h"

NetDnsResult net_dns_resolve_sync(const String host, NetIp* out) {
  (void)host;
  (void)out;
  return NetDnsResult_UnknownError;
}
