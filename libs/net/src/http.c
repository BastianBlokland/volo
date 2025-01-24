#include "core_alloc.h"
#include "core_ascii.h"
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

static void http_write_sync(NetHttp* http, const String data) {
  diag_assert(http->status == NetResult_Success);
  if (http->tls) {
    http->status = net_tls_write_sync(http->tls, http->socket, data);
  } else {
    http->status = net_socket_write_sync(http->socket, data);
  }
}

static void http_read_sync(NetHttp* http) {
  diag_assert(http->status == NetResult_Success);
  if (http->tls) {
    http->status = net_tls_read_sync(http->tls, http->socket, &http->readBuffer);
  } else {
    http->status = net_socket_read_sync(http->socket, &http->readBuffer);
  }
}

static bool http_read_match(NetHttp* http, const String ref) {
  while (http->status == NetResult_Success) {
    const String data = string_consume(dynstring_view(&http->readBuffer), http->readCursor);
    if (data.size >= ref.size) {
      if (string_starts_with(data, ref)) {
        http->readCursor += ref.size;
        return true;
      }
      return false; // No match.
    }
    http_read_sync(http);
  }
  return false; // Error ocurred.
}

static u64 http_read_integer(NetHttp* http) {
  while (http->status == NetResult_Success) {
    const String data       = string_consume(dynstring_view(&http->readBuffer), http->readCursor);
    usize        digitCount = 0;
    u64          result     = 0;
    for (;;) {
      if (digitCount == data.size) {
        goto NeedData;
      }
      const u8 ch = *string_at(data, digitCount);
      if (ascii_is_digit(ch)) {
        result = result * 10 + ascii_to_integer(ch);
        ++digitCount;
        continue;
      }
      if (digitCount) {
        http->readCursor += digitCount;
        return result;
      }
      return sentinel_u64; // Not an integer.
    }
  NeedData:
    http_read_sync(http);
  }
  return sentinel_u64; // Error ocurred.
}

static void http_read_clear(NetHttp* http) {
  dynstring_clear(&http->readBuffer);
  http->readCursor = 0;
}

static String http_read_until(NetHttp* http, const String pattern) {
  while (http->status == NetResult_Success) {
    const String text = string_consume(dynstring_view(&http->readBuffer), http->readCursor);
    const usize  pos  = string_find_first(text, pattern);
    if (!sentinel_check(pos)) {
      http->readCursor += pos + pattern.size;
      return string_slice(text, 0, pos + pattern.size);
    }
    http_read_sync(http);
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
  /**
   * Send request.
   */
  DynString headerBuffer = dynstring_create(g_allocScratch, 4 * usize_kibibyte);
  fmt_write(&headerBuffer, "GET {} HTTP/1.1\r\n", fmt_text(uri));
  fmt_write(&headerBuffer, "Host: {}\r\n", fmt_text(http->host));
  fmt_write(&headerBuffer, "Connection: keep-alive\r\n");
  // fmt_write(&headerBuffer, "Accept-Encoding: gzip, deflate, identity\r\n");
  fmt_write(&headerBuffer, "Accept-Language: en-US\r\n");
  fmt_write(&headerBuffer, "Accept-Charset: utf-8\r\n");
  fmt_write(&headerBuffer, "User-Agent: Volo\r\n");
  fmt_write(&headerBuffer, "\r\n");

  http_write_sync(http, dynstring_view(&headerBuffer));
  if (http->status != NetResult_Success) {
    return http->status;
  }

  /**
   * Handle response.
   */
  if (!http_read_match(http, string_lit("HTTP"))) {
    return http->status ? http->status : NetResult_HttpUnsupportedProtocol;
  }
  if (!http_read_match(http, string_lit("/1.1"))) {
    return http->status ? http->status : NetResult_HttpUnsupportedVersion;
  }
  if (!http_read_match(http, string_lit(" "))) {
    return http->status ? http->status : NetResult_HttpMalformedHeader;
  }
  const u64 status = http_read_integer(http);
  if (sentinel_check(status)) {
    return http->status ? http->status : NetResult_HttpMalformedHeader;
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
  if (tlsRes != NetResult_Success) {
    log_w("Http: Failed to shutdown Tls", log_param("error", fmt_text(net_result_str(tlsRes))));
  }

  NetResult sockRes = NetResult_Success;
  if (http->socket) {
    sockRes = net_socket_shutdown(http->socket, NetDir_Both);
  }
  if (sockRes != NetResult_Success) {
    log_w("Http: Failed to shutdown socket", log_param("error", fmt_text(net_result_str(sockRes))));
  }

  return tlsRes != NetResult_Success ? tlsRes : sockRes;
}
