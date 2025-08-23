#include "check/spec.h"
#include "core/array.h"
#include "net/addr.h"
#include "net/result.h"

spec(addr) {
  it("fails to resolve an empty host") {
    NetAddr         addrs[8];
    u32             addrCount = array_elems(addrs);
    const NetResult res       = net_resolve_sync(string_empty, addrs, &addrCount);
    check_eq_int(res, NetResult_InvalidHost);
    check_eq_int(addrCount, 0);
  }

  it("can resolve localhost") {
    NetAddr         addrs[8];
    u32             addrCount = array_elems(addrs);
    const NetResult res       = net_resolve_sync(string_lit("localhost"), addrs, &addrCount);
    check_eq_int(res, NetResult_Success);
    check(addrCount > 0);
  }

  it("can resolve loopback") {
    NetAddr         addrs[8];
    u32             addrCount = array_elems(addrs);
    const NetResult res       = net_resolve_sync(string_lit("127.0.0.1"), addrs, &addrCount);
    check_eq_int(res, NetResult_Success);
    check(addrCount > 0);
  }

  skip_it("can resolve www.bastian.tech") {
    NetAddr         addrs[8];
    u32             addrCount = array_elems(addrs);
    const NetResult res       = net_resolve_sync(string_lit("www.bastian.tech"), addrs, &addrCount);
    check_eq_int(res, NetResult_Success);
    check(addrCount > 0);
  }

  it("can format addresses") {
    static const struct {
      NetAddr addr;
      String  expected;
    } g_testData[] = {
        {
            .addr     = {.type = NetAddrType_V4, .v4 = {.data = {0, 0, 0, 1}}},
            .expected = string_static("0.0.0.1"),
        },
        {
            .addr     = {.type = NetAddrType_V4, .v4 = {.data = {127, 0, 0, 1}}},
            .expected = string_static("127.0.0.1"),
        },
        {
            .addr     = {.type = NetAddrType_V4, .v4 = {.data = {192, 168, 42, 1}}},
            .expected = string_static("192.168.42.1"),
        },
        {
            .addr     = {.type = NetAddrType_V6, .v6 = {.groups = {0, 0, 0, 0, 0, 0, 0, 0}}},
            .expected = string_static("::"),
        },
        {
            .addr     = {.type = NetAddrType_V6, .v6 = {.groups = {0, 0, 0, 0, 0, 0, 0, 1}}},
            .expected = string_static("::1"),
        },
        {
            .addr     = {.type = NetAddrType_V6, .v6 = {.groups = {1, 2, 3, 4, 5, 6, 7, 8}}},
            .expected = string_static("1:2:3:4:5:6:7:8"),
        },
        {
            .addr     = {.type = NetAddrType_V6, .v6 = {.groups = {1, 0, 0, 0, 0, 0, 0, 8}}},
            .expected = string_static("1::8"),
        },
        {
            .addr     = {.type = NetAddrType_V6, .v6 = {.groups = {0, 2, 3, 4, 5, 6, 7, 8}}},
            .expected = string_static("::2:3:4:5:6:7:8"),
        },
        {
            .addr     = {.type = NetAddrType_V6, .v6 = {.groups = {0, 0, 3, 0, 0, 6, 7, 8}}},
            .expected = string_static("::3:0:0:6:7:8"),
        },
        {
            .addr     = {.type = NetAddrType_V6, .v6 = {.groups = {1, 0, 0, 0, 0, 0, 0, 0}}},
            .expected = string_static("1::"),
        },
        {
            .addr =
                {.type = NetAddrType_V6,
                 .v6 =
                     {.groups = {0x2001, 0x0DB8, 0x0000, 0x0000, 0x0000, 0xFF00, 0x0042, 0x8329}}},
            .expected = string_static("2001:DB8::FF00:42:8329"),
        },
    };
    for (u32 i = 0; i != array_elems(g_testData); ++i) {
      check_eq_string(net_addr_str_scratch(&g_testData[i].addr), g_testData[i].expected);
    }
  }

  it("can format endpoints") {
    static const struct {
      NetEndpoint endpoint;
      String      expected;
    } g_testData[] = {
        {
            .endpoint =
                {
                    .addr = {.type = NetAddrType_V4, .v4 = {.data = {0, 0, 0, 1}}},
                    .port = 42,
                },
            .expected = string_static("0.0.0.1:42"),
        },
        {
            .endpoint =
                {
                    .addr = {.type = NetAddrType_V6, .v6 = {.groups = {0, 0, 0, 0, 0, 0, 0, 1}}},
                    .port = 42,
                },
            .expected = string_static("[::1]:42"),
        },
    };
    for (u32 i = 0; i != array_elems(g_testData); ++i) {
      check_eq_string(net_endpoint_str_scratch(&g_testData[i].endpoint), g_testData[i].expected);
    }
  }

  it("can detect loopback addrs") {
    for (NetAddrType addrType = 0; addrType != NetAddrType_Count; ++addrType) {
      const NetAddr addr = net_addr_loopback(addrType);
      check(net_is_loopback(addr));
    }
  }
}
