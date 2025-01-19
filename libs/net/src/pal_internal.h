#pragma once
#include "net_dns.h"

void net_pal_init(void);
void net_pal_teardown(void);

NetDnsResult net_pal_dns_resolve_sync(String host, NetDnsService service, NetIp* out);
