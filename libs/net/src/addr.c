#include "core/alloc.h"
#include "core/array.h"
#include "core/diag.h"
#include "core/dynstring.h"
#include "core/format.h"
#include "net/addr.h"

bool net_is_loopback(const NetAddr addr) {
  switch (addr.type) {
  case NetAddrType_V4:
    return addr.v4.data[0] == 127;
  case NetAddrType_V6:
    return !addr.v6.groups[0] && !addr.v6.groups[1] && !addr.v6.groups[2] && !addr.v6.groups[3] &&
           !addr.v6.groups[4] && !addr.v6.groups[5] && !addr.v6.groups[6] && addr.v6.groups[7] == 1;
  case NetAddrType_Count:
    break;
  }
  UNREACHABLE
}

bool net_is_linklocal(const NetAddr addr) {
  switch (addr.type) {
  case NetAddrType_V4:
    return addr.v4.data[0] == 169 && addr.v4.data[1] == 254;
  case NetAddrType_V6:
    return addr.v6.groups[0] == 0xfe80;
  case NetAddrType_Count:
    break;
  }
  UNREACHABLE
}

NetAddr net_addr_loopback(const NetAddrType type) {
  switch (type) {
  case NetAddrType_V4:
    return (NetAddr){
        .type = NetAddrType_V4,
        .v4   = {
              .data = {127, 0, 0, 1},
        }};
  case NetAddrType_V6:
    return (NetAddr){
        .type = NetAddrType_V6,
        .v6   = {
              .groups = {0, 0, 0, 0, 0, 0, 0, 1},
        }};
  case NetAddrType_Count:
    break;
  }
  diag_crash_msg("Unsupported ip-type");
}

static void net_addr4_str(const NetAddr4* addr, DynString* out) {
  fmt_write(
      out,
      "{}.{}.{}.{}",
      fmt_int(addr->data[0]),
      fmt_int(addr->data[1]),
      fmt_int(addr->data[2]),
      fmt_int(addr->data[3]));
}

static void net_addr6_str(const NetAddr6* addr, DynString* out) {
  const FormatOptsInt fmtIntOpts = {
      .base      = 16,
      .minDigits = 0, // No leading zeroes.
  };
  bool emptyBlockActive = false, anyEmptyBlock = false;
  for (u32 i = 0; i != array_elems(addr->groups); ++i) {
    const u16 val = addr->groups[i];
    if (!val && emptyBlockActive) {
      continue; // Group will be part of the current empty block.
    }
    if (!val && !anyEmptyBlock) {
      dynstring_append(out, string_lit("::"));
      anyEmptyBlock = emptyBlockActive = true;
      continue; // Start of empty block.
    }
    if (val && emptyBlockActive) {
      emptyBlockActive = false; // End of empty block.
    } else if (i != 0) {
      dynstring_append_char(out, ':');
    }
    format_write_u64(out, addr->groups[i], &fmtIntOpts);
  }
}

void net_addr_str(const NetAddr* addr, DynString* out) {
  switch (addr->type) {
  case NetAddrType_V4:
    net_addr4_str(&addr->v4, out);
    return;
  case NetAddrType_V6:
    net_addr6_str(&addr->v6, out);
    return;
  case NetAddrType_Count:
    break;
  }
  diag_crash_msg("Unsupported address type");
}

String net_addr_str_scratch(const NetAddr* addr) {
  Mem       bufferMem = alloc_alloc(g_allocScratch, usize_kibibyte, 1);
  DynString buffer    = dynstring_create_over(bufferMem);

  net_addr_str(addr, &buffer);

  return dynstring_view(&buffer);
}

void net_endpoint_str(const NetEndpoint* endpoint, DynString* out) {
  if (endpoint->addr.type == NetAddrType_V6) {
    dynstring_append_char(out, '[');
  }
  net_addr_str(&endpoint->addr, out);
  if (endpoint->addr.type == NetAddrType_V6) {
    dynstring_append_char(out, ']');
  }
  fmt_write(out, ":{}", fmt_int(endpoint->port));
}

String net_endpoint_str_scratch(const NetEndpoint* endpoint) {
  Mem       bufferMem = alloc_alloc(g_allocScratch, usize_kibibyte, 1);
  DynString buffer    = dynstring_create_over(bufferMem);

  net_endpoint_str(endpoint, &buffer);

  return dynstring_view(&buffer);
}
