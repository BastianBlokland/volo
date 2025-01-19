#include "check_spec.h"
#include "net_addr.h"
#include "net_dns.h"

spec(dns) {
  it("fails to resolve an empty host") {
    NetIp              ip;
    const NetDnsResult res = net_dns_resolve_sync(string_empty, &ip);
    check_eq_int(res, NetDnsResult_InvalidHost);
  }

  it("can resolve localhost") {
    NetIp              ip;
    const NetDnsResult res = net_dns_resolve_sync(string_lit("localhost"), &ip);
    check_eq_int(res, NetDnsResult_Success);
  }

  it("can resolve loopback") {
    NetIp              ip;
    const NetDnsResult res = net_dns_resolve_sync(string_lit("127.0.0.1"), &ip);
    check_eq_int(res, NetDnsResult_Success);
  }

  skip_it("can resolve google.com") {
    NetIp              ip;
    const NetDnsResult res = net_dns_resolve_sync(string_lit("www.google.com"), &ip);
    check_eq_int(res, NetDnsResult_Success);
  }
}
