#include "core_alloc.h"
#include "core_diag.h"
#include "core_dynstring.h"
#include "net_addr.h"
#include "net_http.h"
#include "net_result.h"
#include "net_socket.h"
#include "net_tls.h"
#include "net_types.h"

/**
 * HTTP (Hypertext Transfer Protocol) client implementation.
 *
 * Aims for supporting a subset of HTTP/1.1, RFC 9112
 * Specification: https://datatracker.ietf.org/doc/html/rfc9112
 */

typedef struct sNetHttp {
  Allocator* alloc;
  NetSocket* socket;
  NetTls*    tls;  // Only valid when using Https.
  String     host; // Hostname of the target server.
  NetResult  status;
} NetHttp;

static u16 http_port(const NetHttpFlags flags) {
  if (flags & NetHttpFlags_Tls) {
    return 443;
  }
  return 80;
}

static NetTlsFlags http_tls_flags(const NetHttpFlags flags) {
  if (flags & NetHttpFlags_TlsNoVerify) {
    return NetTlsFlags_NoVerify;
  }
  return NetTlsFlags_None;
}

static NetResult http_send_sync(NetHttp* http, const String data) {
  diag_assert(http->status == NetResult_Success);
  if (http->tls) {
    return net_tls_write_sync(http->tls, http->socket, data);
  }
  return net_socket_write_sync(http->socket, data);
}

NetHttp* net_http_connect_sync(Allocator* alloc, const String host, const NetHttpFlags flags) {
  NetHttp* http = alloc_alloc_t(alloc, NetHttp);
  *http         = (NetHttp){.alloc = alloc, .host = string_maybe_dup(alloc, host)};

  NetIp hostIp;
  http->status = net_resolve_sync(host, &hostIp);
  if (http->status != NetResult_Success) {
    return http;
  }
  const NetAddr hostAddr = {.ip = hostIp, .port = http_port(flags)};

  http->socket = net_socket_connect_sync(alloc, hostAddr);
  http->status = net_socket_status(http->socket);
  if (http->status != NetResult_Success) {
    return http;
  }

  if (flags & NetHttpFlags_Tls) {
    http->tls    = net_tls_create(alloc, http_tls_flags(flags));
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
  string_maybe_free(http->alloc, http->host);
  alloc_free_t(http->alloc, http);
}

NetResult net_http_status(const NetHttp* http) { return http->status; }

NetResult net_http_get_sync(NetHttp* http, const String uri, DynString* out) {
  if (http->status != NetResult_Success) {
    return http->status;
  }
  DynString headerBuffer = dynstring_create(g_allocScratch, 4 * usize_kibibyte);
  fmt_write(&headerBuffer, "GET {} HTTP/1.1\r\n", fmt_text(uri));
  fmt_write(&headerBuffer, "Host: {}\r\n", fmt_text(http->host));
  fmt_write(&headerBuffer, "Connection: keep-alive\r\n");
  // fmt_write(&headerBuffer, "Accept-Encoding: gzip, deflate, identity\r\n");
  fmt_write(&headerBuffer, "Accept-Language: en-US\r\n");
  fmt_write(&headerBuffer, "Accept-Charset: utf-8\r\n");
  fmt_write(&headerBuffer, "User-Agent: Volo\r\n");
  fmt_write(&headerBuffer, "\r\n");

  http->status = http_send_sync(http, dynstring_view(&headerBuffer));
  if (http->status != NetResult_Success) {
    return http->status;
  }

  // TODO: Read response.
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
