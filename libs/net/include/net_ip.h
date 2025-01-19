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

typedef struct {
  u8 data[16];
} NetIp6;

ASSERT(sizeof(NetIp6) == 16, "Incorrect Ip-v6 size");

typedef struct {
  NetIpType type;
  union {
    NetIp4 v4;
    NetIp6 v6;
  };
} NetIp;
