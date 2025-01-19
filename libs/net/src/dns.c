#include "core_array.h"
#include "core_diag.h"
#include "net_dns.h"

#include "pal_internal.h"

static const String g_dnsResultStrs[] = {
    [NetDnsResult_Success]            = string_static("Success"),
    [NetDnsResult_SystemFailure]      = string_static("SystemFailure"),
    [NetDnsResult_UnsupportedService] = string_static("UnsupportedService"),
    [NetDnsResult_NoEntry]            = string_static("NoEntry"),
    [NetDnsResult_InvalidHost]        = string_static("InvalidHost"),
    [NetDnsResult_HostNotFound]       = string_static("HostNotFound"),
    [NetDnsResult_TryAgain]           = string_static("TryAgain"),
    [NetDnsResult_UnknownError]       = string_static("UnknownError"),
};

ASSERT(array_elems(g_dnsResultStrs) == NetDnsResult_Count, "Incorrect number of result strings");

String net_dns_result_str(const NetDnsResult result) {
  diag_assert(result < NetDnsResult_Count);
  return g_dnsResultStrs[result];
}

String net_dns_service_name(const NetDnsService service) {
  switch (service) {
  case NetDnsService_Http:
    return string_lit("http");
  case NetDnsService_Https:
    return string_lit("https");
  }
  diag_crash_msg("Unknown dns service");
}

NetDnsResult net_dns_resolve_sync(const String host, const NetDnsService service, NetAddr* out) {
  return net_pal_dns_resolve_sync(host, service, out);
}
