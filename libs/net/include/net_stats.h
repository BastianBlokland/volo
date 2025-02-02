#pragma once
#include "net.h"

/**
 * Global network statistics.
 */
typedef struct sNetStats {
  u64 totalResolves, totalConnects;
  u64 totalBytesRead, totalBytesWrite;
} NetStats;

NetStats net_stats_query(void);
