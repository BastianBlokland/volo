#include "core_alloc.h"
#include "net_result.h"
#include "net_tls.h"

#include "tls_internal.h"

typedef struct sNetTls {
  Allocator* alloc;
  NetSocket* socket;
  NetResult  status;
} NetTls;

void net_tls_init(void) {}

void net_tls_teardown(void) {}

NetTls* net_tls_connect_sync(Allocator* alloc, NetSocket* socket) {
  (void)socket;

  NetTls* tls = alloc_alloc_t(alloc, NetTls);

  *tls = (NetTls){.alloc = alloc, .socket = socket};

  return tls;
}

void net_tls_destroy(NetTls* tls) { alloc_free_t(tls->alloc, tls); }

NetResult net_tls_status(const NetTls* tls) { return tls->status; }

NetResult net_tls_write_sync(NetTls* tls, const String data) {
  if (tls->status != NetResult_Success) {
    return tls->status;
  }
  (void)data;
  return NetResult_UnknownError;
}

NetResult net_tls_read_sync(NetTls* s, DynString* out) {
  if (s->status != NetResult_Success) {
    return s->status;
  }
  (void)out;
  return NetResult_UnknownError;
}
