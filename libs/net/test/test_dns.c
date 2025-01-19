#include "check_spec.h"
#include "net_addr.h"
#include "net_dns.h"

spec(dns) {
  it("fails to resolve an empty host") {
    NetAddr            addr;
    const NetDnsResult res = net_dns_resolve_sync(string_empty, NetDnsService_Http, &addr);
    check_eq_int(res, NetDnsResult_InvalidHost);
  }

  it("can resolve localhost") {
    NetAddr            addr;
    const NetDnsResult res =
        net_dns_resolve_sync(string_lit("localhost"), NetDnsService_Http, &addr);
    check_eq_int(res, NetDnsResult_Success);
  }

  it("can resolve loopback") {
    NetAddr            addr;
    const NetDnsResult res =
        net_dns_resolve_sync(string_lit("127.0.0.1"), NetDnsService_Http, &addr);
    check_eq_int(res, NetDnsResult_Success);
  }
}
