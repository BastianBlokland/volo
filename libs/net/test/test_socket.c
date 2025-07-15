#include "check_spec.h"
#include "core_alloc.h"
#include "core_dynstring.h"
#include "net_addr.h"
#include "net_result.h"
#include "net_socket.h"
#include "net_tls.h"
#include "net_types.h"

spec(socket) {
  skip_it("can open an Ipv4 / Ipv6 Tcp connection") {
    /**
     * Test writing a message to a locally running echo server.
     * Prior to running this test start an echo server.
     * For example: ncat -l 6666 -k -c 'xargs -l1 echo'
     */
    const String msg = string_lit("Hello World!\n");

    for (NetIpType ipType = 0; ipType != NetIpType_Count; ++ipType) {
      const NetEndpoint endpoint = {.ip = net_ip_loopback(ipType), .port = 6666};
      NetSocket*        socket   = net_socket_connect_sync(g_allocHeap, endpoint);
      check_eq_int(net_socket_status(socket), NetResult_Success);

      check_eq_int(net_socket_write_sync(socket, msg), NetResult_Success);

      DynString readBuffer = dynstring_create(g_allocScratch, usize_kibibyte);
      check_eq_int(net_socket_read_sync(socket, &readBuffer), NetResult_Success);

      check_eq_string(dynstring_view(&readBuffer), msg);

      check_eq_int(net_socket_shutdown(socket, NetDir_Both), NetResult_Success);
      check_eq_int(net_socket_status(socket), NetResult_ConnectionClosed);
      check_eq_int(net_socket_read_sync(socket, &readBuffer), NetResult_ConnectionClosed);
      check_eq_int(net_socket_write_sync(socket, msg), NetResult_ConnectionClosed);

      net_socket_destroy(socket);
    }
  }

  skip_it("can open an Ipv4 / Ipv6 Tls connection") {
    /**
     * Test writing a message to a locally running echo server using Tls.
     * Prior to running this test start an echo server using Tls (aka ssl).
     * For example: ncat -l --ssl 6666 -k -c 'xargs -l1 echo'
     */
    const String msg = string_lit("Hello World!\n");

    for (NetIpType ipType = 0; ipType != NetIpType_Count; ++ipType) {
      const NetEndpoint endpoint = {.ip = net_ip_loopback(ipType), .port = 6666};
      NetSocket*        socket   = net_socket_connect_sync(g_allocHeap, endpoint);
      check_eq_int(net_socket_status(socket), NetResult_Success);

      NetTls* tls = net_tls_create(g_allocHeap, string_empty /* host */, NetTlsFlags_NoVerify);
      check_eq_int(net_tls_status(tls), NetResult_Success);

      check_eq_int(net_tls_write_sync(tls, socket, msg), NetResult_Success);

      DynString readBuffer = dynstring_create(g_allocScratch, usize_kibibyte);
      check_eq_int(net_tls_read_sync(tls, socket, &readBuffer), NetResult_Success);

      check_eq_string(dynstring_view(&readBuffer), msg);

      check_eq_int(net_tls_shutdown_sync(tls, socket), NetResult_Success);
      check_eq_int(net_tls_status(tls), NetResult_TlsClosed);
      check_eq_int(net_tls_read_sync(tls, socket, &readBuffer), NetResult_TlsClosed);
      check_eq_int(net_tls_write_sync(tls, socket, msg), NetResult_TlsClosed);

      net_tls_destroy(tls);
      check_eq_int(net_socket_shutdown(socket, NetDir_Both), NetResult_Success);
      net_socket_destroy(socket);
    }
  }
}
