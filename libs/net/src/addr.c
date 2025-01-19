#include "core_alloc.h"
#include "core_array.h"
#include "core_diag.h"
#include "core_dynstring.h"
#include "core_format.h"
#include "net_addr.h"

NetIp net_ip_loopback(const NetIpType type) {
  switch (type) {
  case NetIpType_V4:
    return (NetIp){
        .type = NetIpType_V4,
        .v4   = {
              .data = {127, 0, 0, 1},
        }};
  case NetIpType_V6:
    return (NetIp){
        .type = NetIpType_V6,
        .v6   = {
              .groups = {0, 0, 0, 0, 0, 0, 0, 1},
        }};
  }
}

static void net_ip4_str(const NetIp4* ip, DynString* out) {
  fmt_write(
      out,
      "{}.{}.{}.{}",
      fmt_int(ip->data[0]),
      fmt_int(ip->data[1]),
      fmt_int(ip->data[2]),
      fmt_int(ip->data[3]));
}

static void net_ip6_str(const NetIp6* ip, DynString* out) {
  const FormatOptsInt fmtIntOpts = {
      .base      = 16,
      .minDigits = 0, // No leading zeroes.
  };
  bool emptyBlockActive = false, anyEmptyBlock = false;
  for (u32 i = 0; i != array_elems(ip->groups); ++i) {
    const u16 val = ip->groups[i];
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
    }
    if (i) {
      dynstring_append_char(out, ':');
    }
    format_write_u64(out, ip->groups[i], &fmtIntOpts);
  }
}

void net_ip_str(const NetIp* ip, DynString* out) {
  switch (ip->type) {
  case NetIpType_V4:
    return net_ip4_str(&ip->v4, out);
  case NetIpType_V6:
    return net_ip6_str(&ip->v6, out);
  }
  diag_crash_msg("Unsupported ip type");
}

String net_ip_str_scratch(const NetIp* ip) {
  Mem       bufferMem = alloc_alloc(g_allocScratch, usize_kibibyte, 1);
  DynString buffer    = dynstring_create_over(bufferMem);

  net_ip_str(ip, &buffer);

  return dynstring_view(&buffer);
}

void net_addr_str(const NetAddr* addr, DynString* out) {
  if (addr->ip.type == NetIpType_V6) {
    dynstring_append_char(out, '[');
  }
  net_ip_str(&addr->ip, out);
  if (addr->ip.type == NetIpType_V6) {
    dynstring_append_char(out, ']');
  }
  fmt_write(out, ":{}", fmt_int(addr->port));
}

String net_addr_str_scratch(const NetAddr* addr) {
  Mem       bufferMem = alloc_alloc(g_allocScratch, usize_kibibyte, 1);
  DynString buffer    = dynstring_create_over(bufferMem);

  net_addr_str(addr, &buffer);

  return dynstring_view(&buffer);
}
