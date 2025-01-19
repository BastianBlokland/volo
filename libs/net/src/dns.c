#include "core_diag.h"
#include "net_dns.h"

#include "pal_internal.h"

String net_dns_service_name(const NetDnsService service) {
  switch (service) {
  case NetDnsService_Http:
    return string_lit("http");
  case NetDnsService_Https:
    return string_lit("https");
  }
  diag_crash_msg("Unknown dns service");
}

NetDnsResult net_dns_resolve_sync(const String host, const NetDnsService service, NetIp* out) {
  return net_pal_dns_resolve_sync(host, service, out);
}
