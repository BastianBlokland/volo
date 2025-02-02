#include "net_stats.h"

#include "pal_internal.h"

NetStats net_stats_query(void) {
  return (NetStats){
      .totalBytesRead    = net_pal_total_bytes_read(),
      .totalBytesWritten = net_pal_total_bytes_written(),
  };
}
