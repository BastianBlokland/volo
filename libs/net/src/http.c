#include "core_alloc.h"
#include "core_diag.h"
#include "core_dynstring.h"
#include "log_logger.h"
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
  Allocator*   alloc;
  NetSocket*   socket;
  NetTls*      tls;  // Only valid when using Https.
  String       host; // Hostname of the target server.
  NetHttpFlags flags;
  NetResult    status;
  DynString    readBuffer;
  usize        readCursor;
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

static NetResult http_write_sync(NetHttp* http, const String data) {
  diag_assert(http->status == NetResult_Success);
  if (http->tls) {
    return net_tls_write_sync(http->tls, http->socket, data);
  }
  return net_socket_write_sync(http->socket, data);
}

static NetResult http_read_sync(NetHttp* http) {
  diag_assert(http->status == NetResult_Success);
  if (http->tls) {
    return net_tls_read_sync(http->tls, http->socket, &http->readBuffer);
  }
  return net_socket_read_sync(http->socket, &http->readBuffer);
}

static void http_read_clear(NetHttp* http) {
  dynstring_clear(&http->readBuffer);
  http->readCursor = 0;
}

static String http_read_remaining(NetHttp* http) {
  const String full = dynstring_view(&http->readBuffer);
  return string_slice(full, http->readCursor, full.size - http->readCursor);
}

static String http_read_until(NetHttp* http, const String pattern) {
  while (http->status == NetResult_Success) {
    const String text = http_read_remaining(http);
    const usize  pos  = string_find_first(text, pattern);
    if (!sentinel_check(pos)) {
      http->readCursor += pos + pattern.size;
      return string_slice(text, 0, pos + pattern.size);
    }
    http->status = http_read_sync(http);
  }
  return string_empty;
}

NetHttp* net_http_connect_sync(Allocator* alloc, const String host, const NetHttpFlags flags) {
  NetHttp* http = alloc_alloc_t(alloc, NetHttp);

  *http = (NetHttp){
      .alloc      = alloc,
      .host       = string_maybe_dup(alloc, host),
      .readBuffer = dynstring_create(alloc, 16 * usize_kibibyte),
      .flags      = flags,
  };

  log_d("Http: Resolving host", log_param("host", fmt_text(host)));

  NetIp hostIp;
  http->status = net_resolve_sync(host, &hostIp);
  if (http->status != NetResult_Success) {
    log_w(
        "Http: Failed to resolve host",
        log_param("error", fmt_text(net_result_str(http->status))),
        log_param("host", fmt_text(host)));
    return http;
  }
  const NetAddr hostAddr = {.ip = hostIp, .port = http_port(flags)};

  log_d("Http: Connecting to host", log_param("addr", fmt_text(net_addr_str_scratch(&hostAddr))));

  http->socket = net_socket_connect_sync(alloc, hostAddr);
  http->status = net_socket_status(http->socket);
  if (http->status != NetResult_Success) {
    log_w(
        "Http: Failed to connect to host",
        log_param("error", fmt_text(net_result_str(http->status))),
        log_param("address", fmt_text(net_addr_str_scratch(&hostAddr))));
    return http;
  }

  if (flags & NetHttpFlags_Tls) {
    http->tls    = net_tls_create(alloc, host, http_tls_flags(flags));
    http->status = net_tls_status(http->tls);
    if (http->status != NetResult_Success) {
      log_w(
          "Http: Failed to create Tls session",
          log_param("error", fmt_text(net_result_str(http->status))));
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
  dynstring_destroy(&http->readBuffer);
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

  http->status = http_write_sync(http, dynstring_view(&headerBuffer));
  if (http->status != NetResult_Success) {
    return http->status;
  }

  const String headerData = http_read_until(http, string_lit("\r\n\r\n"));
  if (http->status != NetResult_Success) {
    return http->status;
  }

  // TODO: Read response.
  (void)headerData;
  (void)out;

  // TODO: Check if the server send us unexpected data.
  http_read_clear(http);

  return NetResult_Success;
}

NetResult net_http_shutdown_sync(NetHttp* http) {
  log_d("Http: Shutdown");

  NetResult tlsRes = NetResult_Success;
  if (http->tls) {
    tlsRes = net_tls_shutdown_sync(http->tls, http->socket);
  }
  log_w("Http: Failed to shutdown Tls", log_param("error", fmt_text(net_result_str(tlsRes))));

  NetResult sockRes = NetResult_Success;
  if (http->socket) {
    sockRes = net_socket_shutdown(http->socket, NetDir_Both);
  }
  log_w("Http: Failed to shutdown socket", log_param("error", fmt_text(net_result_str(sockRes))));

  return tlsRes != NetResult_Success ? tlsRes : sockRes;
}
