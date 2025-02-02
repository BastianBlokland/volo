#pragma once
#include "net.h"

/**
 * Global network statistics.
 */
typedef struct sNetStats {
  u64 totalBytesRead;
  u64 totalBytesWritten;
} NetStats;

NetStats net_stats_query(void);
