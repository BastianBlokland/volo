#pragma once
#include "net.h"

typedef enum {
  NetIpType_V4,
  NetIpType_V6,

  NetIpType_Count,
} NetIpType;

typedef struct {
  u8 data[4];
} NetIp4;

ASSERT(sizeof(NetIp4) == 4, "Incorrect Ip-v4 size");

typedef union {
  u16 groups[8];
  u8  data[16];
} NetIp6;

ASSERT(sizeof(NetIp6) == 16, "Incorrect Ip-v6 size");

typedef struct sNetIp {
  NetIpType type;
  union {
    NetIp4 v4;
    NetIp6 v6;
  };
} NetIp;

typedef struct sNetAddr {
  NetIp ip;
  u16   port;
} NetAddr;

/**
 * Return the loopback address.
 */
NetIp net_ip_loopback(NetIpType);

/**
 * Lookup the current ip addresses of the active network interfaces (excluding loop-back).
 * NOTE: Provide null to the 'out' parameter to query the interface count without returning the ips.
 */
u32 net_ip_interfaces(NetIp out[], u32 outMax);

/**
 * Synchonously resolve a host-name to an ip-address.
 */
NetResult net_resolve_sync(String host, NetIp* out);

/**
 * Write the textual representation of the given ip.
 */
void   net_ip_str(const NetIp*, DynString* out);
String net_ip_str_scratch(const NetIp*);

/**
 * Write the textual representation of the given address.
 */
void   net_addr_str(const NetAddr*, DynString* out);
String net_addr_str_scratch(const NetAddr*);
