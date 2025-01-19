#include "core_diag.h"
#include "net_addr.h"
#include "net_dns.h"

#include "pal_internal.h"

void net_pal_init(void) {}
void net_pal_teardown(void) {}

NetDnsResult net_pal_dns_resolve_sync(const String host, const NetDnsService srv, NetAddr* out) {
  (void)host;
  (void)srv;
  (void)out;
  return NetDnsResult_UnknownError;
}
