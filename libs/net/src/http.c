#include "core_alloc.h"
#include "net_http.h"
#include "net_result.h"

typedef struct sNetHttp {
  Allocator* alloc;
  NetResult  status;
} NetHttp;

/**
 * TODO:
 */
NetHttp* net_http_connect_sync(Allocator* alloc, const NetHttpProtocol proto, const String host) {
  NetHttp* http = alloc_alloc_t(alloc, NetHttp);

  (void)proto;
  (void)host;
  *http = (NetHttp){.alloc = alloc};

  return http;
}

void net_http_destroy(NetHttp* http) { alloc_free_t(http->alloc, http); }

NetResult net_http_status(const NetHttp* http) { return http->status; }

NetResult net_http_get_sync(NetHttp* http, const String resource, DynString* out) {
  (void)http;
  (void)resource;
  (void)out;
  return NetResult_Success;
}

NetResult net_http_shutdown_sync(NetHttp* http) {
  (void)http;
  return NetResult_Success;
}
