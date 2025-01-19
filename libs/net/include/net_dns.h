#pragma once
#include "net.h"

typedef enum {
  NetDnsService_Http,
  NetDnsService_Https, // Http over TLS/SSL.
} NetDnsService;

typedef enum {
  NetDnsResult_Success = 0,
  NetDnsResult_SystemFailure,
  NetDnsResult_UnsupportedService,
  NetDnsResult_NoEntry,
  NetDnsResult_InvalidHost,
  NetDnsResult_HostNotFound,
  NetDnsResult_TryAgain,
  NetDnsResult_UnknownError,
} NetDnsResult;

/**
 * Get the textual name of a Dns service.
 */
String net_dns_service_name(NetDnsService);

/**
 * Synchonously resolve a host-name to an address for the given service.
 */
NetDnsResult net_dns_resolve_sync(String host, NetDnsService, NetAddr* out);
