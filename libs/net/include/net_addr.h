#pragma once
#include "net.h"

typedef enum {
  NetIpType_V4,
  NetIpType_V6,
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
 * Write the textual representation of the given ip.
 */
void   net_ip_str(const NetIp*, DynString* out);
String net_ip_str_scratch(const NetIp*);

/**
 * Write the textual representation of the given address.
 */
void   net_addr_str(const NetAddr*, DynString* out);
String net_addr_str_scratch(const NetAddr*);
