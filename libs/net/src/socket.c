#include "net_addr.h"
#include "net_result.h"
#include "net_socket.h"

NetSocket*
net_socket_connect_any_sync(Allocator* alloc, const NetIp* ips, const u32 ipCount, const u16 port) {
  NetSocket* result = null;
  for (u32 i = 0; i != ipCount; ++i) {
    if (result) {
      net_socket_destroy(result);
    }
    const NetEndpoint endpoint = (NetEndpoint){.ip = ips[i], .port = port};
    result                     = net_socket_connect_sync(alloc, endpoint);
    if (net_socket_status(result) == NetResult_Success) {
      break;
    }
  }
  return result;
}
