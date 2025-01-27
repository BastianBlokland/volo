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
  NetAddr      hostAddr;
  NetHttpFlags flags;
  NetResult    status;
  DynString    readBuffer;
  usize        readCursor;
} NetHttp;

typedef struct {
  u64    status;
  String reason;
  String server, via;
  String contentType, contentEncoding, contentMd5;
  String transferEncoding;
  u64    age;
  String body;
} NetHttpResponse;

static NetTlsFlags http_tls_flags(const NetHttpFlags flags) {
  if (flags & NetHttpFlags_TlsNoVerify) {
    return NetTlsFlags_NoVerify;
  }
  return NetTlsFlags_None;
}

static NetResult http_status_result(const u64 status) {
  if (status >= 500) {
    return NetResult_HttpServerError;
  }
  if (status >= 400) {
    switch (status) {
    case 401:
      return NetResult_HttpUnauthorized;
    case 403:
      return NetResult_HttpForbidden;
    case 404:
      return NetResult_HttpNotFound;
    default:
      return NetResult_HttpClientError;
    }
  }
  if (status >= 300) {
    return NetResult_HttpRedirected;
  }
  if (status >= 200) {
    return NetResult_Success;
  }
  return NetResult_HttpServerError;
}

static void http_set_err(NetHttp* http, const NetResult err) {
  // NOTE: Don't override a previous error.
  if (http->status == NetResult_Success) {
    http->status = err;
  }
}

static void http_request_get_header(const NetHttp* http, const String uri, DynString* out) {
  fmt_write(out, "GET {} HTTP/1.1\r\n", fmt_text(uri));
  fmt_write(out, "Host: {}\r\n", fmt_text(http->host));
  fmt_write(out, "Connection: keep-alive\r\n");
  fmt_write(out, "Accept-Language: en-US\r\n");
  fmt_write(out, "Accept-Charset: utf-8\r\n");
  fmt_write(out, "\r\n");
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

static String http_read_until(NetHttp* http, const String pattern) {
  while (http->status == NetResult_Success) {
    const String data = string_consume(dynstring_view(&http->readBuffer), http->readCursor);
    const usize  pos  = string_find_first(data, pattern);
    if (!sentinel_check(pos)) {
      http->readCursor += pos + pattern.size;
      return string_slice(data, 0, pos);
    }
    http_read_sync(http);
  }
  return string_empty;
}

static String http_read_sized(NetHttp* http, const usize size) {
  diag_assert(!sentinel_check(size));
  if (!size) {
    return string_empty;
  }
  while (http->status == NetResult_Success) {
    const String data = string_consume(dynstring_view(&http->readBuffer), http->readCursor);
    if (data.size >= size) {
      http->readCursor += size;
      return string_slice(data, 0, size);
    }
    http_read_sync(http);
  }
  return string_empty; // Error occurred.
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

static NetHttpResponse http_read_response(NetHttp* http) {
  NetHttpResponse res = {
      .reason           = string_lit("unknown"),
      .server           = string_lit("unknown"),
      .via              = string_lit("unknown"),
      .contentType      = string_lit("text/plain"),
      .contentEncoding  = string_lit("identity"),
      .transferEncoding = string_lit("identity"),
  };
  if (!http_read_match(http, string_lit("HTTP"))) {
    return http_set_err(http, NetResult_HttpUnsupportedProtocol), res;
  }
  if (!http_read_match(http, string_lit("/1.1"))) {
    return http_set_err(http, NetResult_HttpUnsupportedVersion), res;
  }
  if (!http_read_match(http, string_lit(" "))) {
    return http_set_err(http, NetResult_HttpMalformedHeader), res;
  }
  if (sentinel_check((res.status = http_read_integer(http)))) {
    return http_set_err(http, NetResult_HttpMalformedHeader), res;
  }
  res.reason = string_trim_whitespace(http_read_until(http, string_lit("\r\n")));
  if (http->status != NetResult_Success) {
    return res;
  }
  u64 contentLength = 0;
  for (;;) {
    if (http_read_match(http, string_lit("\r\n"))) {
      break; // End of header.
    }
    const String fieldName = string_trim_whitespace(http_read_until(http, string_lit(":")));
    if (http->status != NetResult_Success || string_is_empty(fieldName)) {
      return http_set_err(http, NetResult_HttpMalformedHeader), res;
    }
    const String fieldValue = string_trim_whitespace(http_read_until(http, string_lit("\r\n")));
    if (http->status != NetResult_Success) {
      return http_set_err(http, NetResult_HttpMalformedHeader), res;
    }
    if (0) {
    } else if (string_eq_no_case(fieldName, string_lit("Server"))) {
      res.server = fieldValue;
    } else if (string_eq_no_case(fieldName, string_lit("Via"))) {
      res.via = fieldValue;
    } else if (string_eq_no_case(fieldName, string_lit("Content-Length"))) {
      format_read_u64(fieldValue, &contentLength, 10 /* base */);
    } else if (string_eq_no_case(fieldName, string_lit("Content-Type"))) {
      res.contentType = fieldValue;
    } else if (string_eq_no_case(fieldName, string_lit("Content-Encoding"))) {
      res.contentEncoding = fieldValue;
    } else if (string_eq_no_case(fieldName, string_lit("Content-MD5"))) {
      res.contentMd5 = fieldValue;
    } else if (string_eq_no_case(fieldName, string_lit("Transfer-Encoding"))) {
      res.transferEncoding = fieldValue;
    } else if (string_eq_no_case(fieldName, string_lit("Age"))) {
      format_read_u64(fieldValue, &res.age, 10 /* base */);
    }
  }
  if (!string_eq_no_case(res.transferEncoding, string_lit("identity"))) {
    return http_set_err(http, NetResult_HttpMalformedHeader), res;
  }
  res.body = http_read_sized(http, contentLength);
  return res;
}

static void http_read_end(NetHttp* http) {
  if (http->readBuffer.size != http->readCursor) {
    http_set_err(http, NetResult_HttpUnexpectedData);
  }
  dynstring_clear(&http->readBuffer);
  http->readCursor = 0;
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
  http->hostAddr = (NetAddr){.ip = hostIp, .port = flags & NetHttpFlags_Tls ? 443 : 80};

  log_d(
      "Http: Connecting to host",
      log_param("addr", fmt_text(net_addr_str_scratch(&http->hostAddr))));

  http->socket = net_socket_connect_sync(alloc, http->hostAddr);
  http->status = net_socket_status(http->socket);
  if (http->status != NetResult_Success) {
    log_w(
        "Http: Failed to connect to host",
        log_param("error", fmt_text(net_result_str(http->status))),
        log_param("address", fmt_text(net_addr_str_scratch(&http->hostAddr))));
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

NetResult      net_http_status(const NetHttp* http) { return http->status; }
const NetAddr* net_http_remote(const NetHttp* http) { return &http->hostAddr; }
String         net_http_remote_name(const NetHttp* http) { return http->host; }

NetResult net_http_get_sync(NetHttp* http, const String uri, DynString* out) {
  if (http->status != NetResult_Success) {
    return http->status;
  }
  const String uriOrRoot = string_is_empty(uri) ? string_lit("/") : uri;

  /**
   * Send request.
   */
  DynString headerBuffer = dynstring_create(g_allocScratch, 4 * usize_kibibyte);
  http_request_get_header(http, uriOrRoot, &headerBuffer);

  log_d(
      "Http: Sending GET",
      log_param("host", fmt_text(http->host)),
      log_param("uri", fmt_text(uriOrRoot)));

  http_write_sync(http, dynstring_view(&headerBuffer));
  if (http->status != NetResult_Success) {
    return http->status;
  }

  /**
   * Handle response.
   */
  NetHttpResponse response = http_read_response(http);
  if (http->status != NetResult_Success) {
    return http->status;
  }

  log_d(
      "Http: Received GET response",
      log_param("status", fmt_int(response.status)),
      log_param("reason", fmt_text(response.reason)),
      log_param("server", fmt_text(response.server)),
      log_param("via", fmt_text(response.via)),
      log_param("content-type", fmt_text(response.contentType)),
      log_param("content-encoding", fmt_text(response.contentEncoding)),
      log_param("transfer-encoding", fmt_text(response.transferEncoding)),
      log_param("age", fmt_int(response.age)),
      log_param("body-size", fmt_size(response.body.size)));

  const NetResult result = http_status_result(response.status);
  if (result == NetResult_Success) {
    dynstring_append(out, response.body);
  }

  http_read_end(http); // Releases reading resources, do not access response data after this.
  return http->status ? http->status : result;
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
