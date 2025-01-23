#include "core_alloc.h"
#include "core_diag.h"
#include "net_addr.h"
#include "net_http.h"
#include "net_result.h"
#include "net_socket.h"
#include "net_tls.h"
#include "net_types.h"

typedef struct sNetHttp {
  Allocator* alloc;
  NetSocket* socket;
  NetTls*    tls; // Only valid when using Https.
  NetResult  status;
} NetHttp;

static u16 http_port(const NetHttpProtocol proto) {
  switch (proto) {
  case NetHttpProtocol_Http:
    return 80;
  case NetHttpProtocol_Https:
    return 443;
  }
  diag_crash_msg("Unsupported protocol");
}

/**
 * TODO:
 */
NetHttp* net_http_connect_sync(Allocator* alloc, const NetHttpProtocol proto, const String host) {
  NetHttp* http = alloc_alloc_t(alloc, NetHttp);
  *http         = (NetHttp){.alloc = alloc};

  NetIp hostIp;
  http->status = net_resolve_sync(host, &hostIp);
  if (http->status != NetResult_Success) {
    return http;
  }
  const NetAddr hostAddr = {.ip = hostIp, .port = http_port(proto)};

  http->socket = net_socket_connect_sync(alloc, hostAddr);
  http->status = net_socket_status(http->socket);
  if (http->status != NetResult_Success) {
    return http;
  }

  if (proto == NetHttpProtocol_Https) {
    http->tls    = net_tls_create(alloc, NetTlsFlags_None);
    http->status = net_tls_status(http->tls);
    if (http->status != NetResult_Success) {
      return http;
    }
  }

  return http;
}

void net_http_destroy(NetHttp* http) {
  if (http->tls) {
    net_tls_destroy(http->tls);
  }
  if (http->socket) {
    net_socket_destroy(http->socket);
  }
  alloc_free_t(http->alloc, http);
}

NetResult net_http_status(const NetHttp* http) { return http->status; }

NetResult net_http_get_sync(NetHttp* http, const String resource, DynString* out) {
  if (http->status != NetResult_Success) {
    return http->status;
  }
  (void)http;
  (void)resource;
  (void)out;
  return NetResult_Success;
}

NetResult net_http_shutdown_sync(NetHttp* http) {
  NetResult tlsRes = NetResult_Success;
  if (http->tls) {
    tlsRes = net_tls_shutdown_sync(http->tls, http->socket);
  }
  NetResult socketRes = NetResult_Success;
  if (http->socket) {
    socketRes = net_socket_shutdown(http->socket, NetDir_Both);
  }
  return tlsRes != NetResult_Success ? tlsRes : socketRes;
}
