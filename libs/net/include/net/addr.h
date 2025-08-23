#pragma once
#include "net/forward.h"

typedef enum {
  NetAddrType_V4,
  NetAddrType_V6,

  NetAddrType_Count,
} NetAddrType;

typedef struct {
  u8 data[4];
} NetAddr4;

ASSERT(sizeof(NetAddr4) == 4, "Incorrect Ip-v4 size");

typedef union {
  u16 groups[8];
  u8  data[16];
} NetAddr6;

ASSERT(sizeof(NetAddr6) == 16, "Incorrect Ip-v6 size");

typedef struct sNetAddr {
  NetAddrType type;
  union {
    NetAddr4 v4;
    NetAddr6 v6;
  };
} NetAddr;

typedef struct sNetEndpoint {
  NetAddr addr;
  u16     port;
} NetEndpoint;

/**
 * Query the attributes for an address.
 */
bool net_is_loopback(NetAddr);
bool net_is_linklocal(NetAddr);

/**
 * Return the loopback address.
 */
NetAddr net_addr_loopback(NetAddrType);

typedef enum {
  NetInterfaceQueryFlags_None             = 0,
  NetInterfaceQueryFlags_IncludeLinkLocal = 1 << 0,
} NetInterfaceQueryFlags;

/**
 * Lookup the current addresses of the active network interfaces (excluding loop-back).
 * NOTE: Provide the max amount to query in 'count'; will be replaced with the result count.
 */
NetResult net_interfaces(NetAddr out[], u32* count, NetInterfaceQueryFlags);

/**
 * Synchonously resolve a host-name to addresses.
 * NOTE: Provide the max amount to query in 'count'; will be replaced with the result count.
 */
NetResult net_resolve_sync(String host, NetAddr out[], u32* count);

/**
 * Write the textual representation of the given ip.
 */
void   net_addr_str(const NetAddr*, DynString* out);
String net_addr_str_scratch(const NetAddr*);

/**
 * Write the textual representation of the given endpoint.
 */
void   net_endpoint_str(const NetEndpoint*, DynString* out);
String net_endpoint_str_scratch(const NetEndpoint*);
