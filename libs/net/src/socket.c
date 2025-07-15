#include "net_addr.h"
#include "net_result.h"
#include "net_socket.h"

NetSocket* net_socket_connect_any_sync(
    Allocator* alloc, const NetEndpoint* endpoints, const u32 endpointCount) {
  NetSocket* result = null;
  for (u32 i = 0; i != endpointCount; ++i) {
    if (result) {
      net_socket_destroy(result);
    }
    result = net_socket_connect_sync(alloc, endpoints[i]);
    if (net_socket_status(result) == NetResult_Success) {
      break;
    }
  }
  return result;
}
