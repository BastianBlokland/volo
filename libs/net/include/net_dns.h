#pragma once
#include "net.h"

typedef enum eNetDnsResult {
  NetDnsResult_Success = 0,
  NetDnsResult_SystemFailure,
  NetDnsResult_UnsupportedService,
  NetDnsResult_NoEntry,
  NetDnsResult_InvalidHost,
  NetDnsResult_HostNotFound,
  NetDnsResult_TryAgain,
  NetDnsResult_UnknownError,

  NetDnsResult_Count,
} NetDnsResult;

/**
 * Return a textual representation of the given NetDnsResult.
 */
String net_dns_result_str(NetDnsResult);

/**
 * Synchonously resolve a host-name to an ip-address.
 */
NetDnsResult net_dns_resolve_sync(String host, NetIp* out);
