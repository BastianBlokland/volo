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

/**
 * View into the http read-buffer.
 * NOTE: Stored as offsets from the start of the buffer to support re-allocating the buffer.
 */
typedef struct {
  usize offset, size;
} NetHttpView;

typedef struct {
  u64         status;
  NetHttpView reason;
  NetHttpView contentEncoding;
  u64         contentLength;
  NetHttpView transferEncoding;
} NetHttpResponse;

static String http_view_str(const NetHttp* http, const NetHttpView view) {
  return string_slice(dynstring_view(&http->readBuffer), view.offset, view.size);
}

static String http_view_str_trim(const NetHttp* http, const NetHttpView view) {
  return string_trim_whitespace(http_view_str(http, view));
}

static bool http_view_eq_loose(const NetHttp* http, const NetHttpView view, const String str) {
  return string_eq_no_case(http_view_str_trim(http, view), str);
}

static NetHttpView http_view_empty(void) { return (NetHttpView){0}; }

static NetHttpView http_view_remaining(const NetHttp* http) {
  return (NetHttpView){
      .offset = http->readCursor,
      .size   = http->readBuffer.size - http->readCursor,
  };
}

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
    const NetHttpView data = http_view_remaining(http);
    if (data.size >= ref.size) {
      if (string_starts_with(http_view_str(http, data), ref)) {
        http->readCursor += ref.size;
        return true;
      }
      return false; // No match.
    }
    http_read_sync(http);
  }
  return false; // Error ocurred.
}

static NetHttpView http_read_until(NetHttp* http, const String pattern) {
  while (http->status == NetResult_Success) {
    const NetHttpView data = http_view_remaining(http);
    const usize       pos  = string_find_first(http_view_str(http, data), pattern);
    if (!sentinel_check(pos)) {
      http->readCursor += pos + pattern.size;
      return (NetHttpView){.offset = data.offset, .size = pos};
    }
    http_read_sync(http);
  }
  return http_view_empty(); // Error occurred.
}

static NetHttpView http_read_sized(NetHttp* http, const usize size) {
  diag_assert(!sentinel_check(size));
  if (!size) {
    return http_view_empty();
  }
  while (http->status == NetResult_Success) {
    const NetHttpView data = http_view_remaining(http);
    if (data.size >= size) {
      http->readCursor += size;
      return (NetHttpView){.offset = data.offset, .size = size};
    }
    http_read_sync(http);
  }
  return http_view_empty(); // Error occurred.
}

static u64 http_read_integer(NetHttp* http, const u8 base) {
  while (http->status == NetResult_Success) {
    const NetHttpView data       = http_view_remaining(http);
    usize             digitCount = 0;
    u64               result     = 0;
    for (;;) {
      if (digitCount == data.size) {
        goto NeedData;
      }
      const u8 ch    = *string_at(http_view_str(http, data), digitCount);
      const u8 digit = ascii_to_integer(ch);
      if (digit < base) {
        result = result * base + digit;
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
  NetHttpResponse resp = {0};
  if (!http_read_match(http, string_lit("HTTP"))) {
    return http_set_err(http, NetResult_HttpUnsupportedProtocol), resp;
  }
  if (!http_read_match(http, string_lit("/1.1"))) {
    return http_set_err(http, NetResult_HttpUnsupportedVersion), resp;
  }
  if (!http_read_match(http, string_lit(" "))) {
    return http_set_err(http, NetResult_HttpMalformedHeader), resp;
  }
  if (sentinel_check((resp.status = http_read_integer(http, 10 /* base */)))) {
    return http_set_err(http, NetResult_HttpMalformedHeader), resp;
  }
  resp.reason = http_read_until(http, string_lit("\r\n"));
  if (http->status != NetResult_Success) {
    return resp;
  }
  for (;;) {
    if (http_read_match(http, string_lit("\r\n"))) {
      break; // End of header.
    }
    const NetHttpView fieldName = http_read_until(http, string_lit(":"));
    if (http->status != NetResult_Success || string_is_empty(fieldName)) {
      return http_set_err(http, NetResult_HttpMalformedHeader), resp;
    }
    const NetHttpView fieldValue = http_read_until(http, string_lit("\r\n"));
    if (http->status != NetResult_Success) {
      return http_set_err(http, NetResult_HttpMalformedHeader), resp;
    }
    if (0) {
    } else if (http_view_eq_loose(http, fieldName, string_lit("Content-Length"))) {
      format_read_u64(http_view_str_trim(http, fieldValue), &resp.contentLength, 10 /* base */);
    } else if (http_view_eq_loose(http, fieldName, string_lit("Content-Encoding"))) {
      resp.contentEncoding = fieldValue;
    } else if (http_view_eq_loose(http, fieldName, string_lit("Transfer-Encoding"))) {
      resp.transferEncoding = fieldValue;
    }
  }
  return resp;
}

static NetHttpView http_read_body(NetHttp* http, const NetHttpResponse* resp) {
  if (!resp->transferEncoding.size /* no transfer-encoding specified */) {
    return http_read_sized(http, resp->contentLength);
  }
  if (http_view_eq_loose(http, resp->transferEncoding, string_lit("identity"))) {
    return http_read_sized(http, resp->contentLength);
  }
  if (http_view_eq_loose(http, resp->transferEncoding, string_lit("chunked"))) {
    const usize dataStart = http->readCursor;
    usize       dataSize  = 0;
    for (;;) {
      const u64 chunkSize = http_read_integer(http, 16 /* base */);
      if (sentinel_check(chunkSize)) {
        return http_set_err(http, NetResult_HttpMalformedChunk), http_view_empty();
      }
      if (!chunkSize) {
        // End of chunked data; skip over chunk comment and potentially trailing headers.
        http_read_until(http, string_lit("\r\n\r\n"));
        return (NetHttpView){.offset = dataStart, .size = dataSize};
      }
      http_read_until(http, string_lit("\r\n")); // Skip over chunk comment.
      if (http->status != NetResult_Success) {
        return http_set_err(http, NetResult_HttpMalformedChunk), http_view_empty();
      }

      // Erase the chunk-metadata from the read-buffer to make sure the result is contiguous.
      const usize dataEnd = dataStart + dataSize;
      diag_assert(http->readCursor > dataEnd);
      dynstring_erase_chars(&http->readBuffer, dataEnd, http->readCursor - dataEnd);
      http->readCursor = dataEnd;

      http_read_sized(http, chunkSize);
      if (!http_read_match(http, string_lit("\r\n"))) {
        return http_set_err(http, NetResult_HttpMalformedChunk), http_view_empty();
      }
      dataSize += chunkSize;
    }
    UNREACHABLE
  }
  return http_set_err(http, NetResult_HttpUnsupportedTransferEncoding), http_view_empty();
}

static void http_read_decode_body(
    NetHttp* http, const NetHttpResponse* resp, const NetHttpView body, DynString* out) {
  if (!resp->contentEncoding.size /* no content encoding specified */) {
    dynstring_append(out, http_view_str(http, body));
    return;
  }
  if (http_view_eq_loose(http, resp->contentEncoding, string_lit("identity"))) {
    dynstring_append(out, http_view_str(http, body));
    return;
  }
  http_set_err(http, NetResult_HttpUnsupportedContentEncoding), http_view_empty();
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
  const NetHttpResponse resp = http_read_response(http);
  if (http->status != NetResult_Success) {
    return http->status;
  }

  log_d(
      "Http: Received GET response",
      log_param("status", fmt_int(resp.status)),
      log_param("reason", fmt_text(http_view_str_trim(http, resp.reason))));

  const NetHttpView body = http_read_body(http, &resp);
  if (http->status != NetResult_Success) {
    return http->status;
  }

  if (http->status == NetResult_Success) {
    log_d("Http: Received GET body", log_param("size", fmt_size(body.size)));
    http_read_decode_body(http, &resp, body, out);
  }

  http_read_end(http); // Releases reading resources, do not access response data after this.
  return http->status ? http->status : http_status_result(resp.status);
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
