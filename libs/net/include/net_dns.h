#pragma once
#include "net.h"

typedef enum {
  NetDnsResult_Success = 0,
  NetDnsResult_UnknownError,
} NetDnsResult;

/**
 * Synchonously resolve a host-name to an Ip-address.
 */
NetDnsResult net_dns_resolve_sync(String host, NetIp* out);
