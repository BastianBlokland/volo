#include "check_spec.h"
#include "core_array.h"
#include "net_addr.h"
#include "net_result.h"

spec(addr) {
  it("fails to resolve an empty host") {
    NetIp           ip;
    const NetResult res = net_resolve_sync(string_empty, &ip);
    check_eq_int(res, NetResult_InvalidHost);
  }

  it("can resolve localhost") {
    NetIp           ip;
    const NetResult res = net_resolve_sync(string_lit("localhost"), &ip);
    check_eq_int(res, NetResult_Success);
  }

  it("can resolve loopback") {
    NetIp           ip;
    const NetResult res = net_resolve_sync(string_lit("127.0.0.1"), &ip);
    check_eq_int(res, NetResult_Success);
  }

  skip_it("can resolve google.com") {
    NetIp           ip;
    const NetResult res = net_resolve_sync(string_lit("www.google.com"), &ip);
    check_eq_int(res, NetResult_Success);
  }

  it("can format ip's") {
    static const struct {
      NetIp  ip;
      String expected;
    } g_testData[] = {
        {
            .ip       = {.type = NetIpType_V4, .v4 = {.data = {0, 0, 0, 1}}},
            .expected = string_static("0.0.0.1"),
        },
        {
            .ip       = {.type = NetIpType_V4, .v4 = {.data = {127, 0, 0, 1}}},
            .expected = string_static("127.0.0.1"),
        },
        {
            .ip       = {.type = NetIpType_V4, .v4 = {.data = {192, 168, 42, 1}}},
            .expected = string_static("192.168.42.1"),
        },
        {
            .ip       = {.type = NetIpType_V6, .v6 = {.groups = {0, 0, 0, 0, 0, 0, 0, 0}}},
            .expected = string_static("::"),
        },
        {
            .ip       = {.type = NetIpType_V6, .v6 = {.groups = {0, 0, 0, 0, 0, 0, 0, 1}}},
            .expected = string_static("::1"),
        },
        {
            .ip       = {.type = NetIpType_V6, .v6 = {.groups = {1, 2, 3, 4, 5, 6, 7, 8}}},
            .expected = string_static("1:2:3:4:5:6:7:8"),
        },
        {
            .ip       = {.type = NetIpType_V6, .v6 = {.groups = {1, 0, 0, 0, 0, 0, 0, 8}}},
            .expected = string_static("1::8"),
        },
        {
            .ip       = {.type = NetIpType_V6, .v6 = {.groups = {0, 2, 3, 4, 5, 6, 7, 8}}},
            .expected = string_static("::2:3:4:5:6:7:8"),
        },
        {
            .ip       = {.type = NetIpType_V6, .v6 = {.groups = {0, 0, 3, 0, 0, 6, 7, 8}}},
            .expected = string_static("::3:0:0:6:7:8"),
        },
        {
            .ip       = {.type = NetIpType_V6, .v6 = {.groups = {1, 0, 0, 0, 0, 0, 0, 0}}},
            .expected = string_static("1::"),
        },
        {
            .ip =
                {.type = NetIpType_V6,
                 .v6 =
                     {.groups = {0x2001, 0x0DB8, 0x0000, 0x0000, 0x0000, 0xFF00, 0x0042, 0x8329}}},
            .expected = string_static("2001:DB8::FF00:42:8329"),
        },
    };
    for (u32 i = 0; i != array_elems(g_testData); ++i) {
      check_eq_string(net_ip_str_scratch(&g_testData[i].ip), g_testData[i].expected);
    }
  }

  it("can format addresses") {
    static const struct {
      NetAddr addr;
      String  expected;
    } g_testData[] = {
        {
            .addr =
                {
                    .ip   = {.type = NetIpType_V4, .v4 = {.data = {0, 0, 0, 1}}},
                    .port = 42,
                },
            .expected = string_static("0.0.0.1:42"),
        },
        {
            .addr =
                {
                    .ip   = {.type = NetIpType_V6, .v6 = {.groups = {0, 0, 0, 0, 0, 0, 0, 1}}},
                    .port = 42,
                },
            .expected = string_static("[::1]:42"),
        },
    };
    for (u32 i = 0; i != array_elems(g_testData); ++i) {
      check_eq_string(net_addr_str_scratch(&g_testData[i].addr), g_testData[i].expected);
    }
  }
}
