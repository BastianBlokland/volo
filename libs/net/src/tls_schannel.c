#include "core_alloc.h"
#include "core_diag.h"
#include "core_dynstring.h"
#include "log_logger.h"
#include "net_result.h"
#include "net_socket.h"
#include "net_tls.h"

#include "tls_internal.h"

/**
 * TODO:
 */

typedef struct {
  u32 dummy;
} NetSchannel;

static bool net_schannel_init(NetSchannel* schannel, Allocator* alloc) {
  (void)schannel;
  (void)alloc;
  return true;
}

static NetSchannel g_netSchannel;
static bool        g_netSchannelReady;

void net_tls_init(void) {
  diag_assert(!g_netSchannelReady);
  g_netSchannelReady = net_schannel_init(&g_netSchannel, g_allocPersist);
}

void net_tls_teardown(void) {
  g_netSchannel      = (NetSchannel){0};
  g_netSchannelReady = false;
}

typedef struct sNetTls {
  Allocator* alloc;
  NetResult  status;
} NetTls;

NetTls* net_tls_create_sync(Allocator* alloc, const String host, const NetTlsFlags flags) {
  NetTls* tls = alloc_alloc_t(alloc, NetTls);

  *tls = (NetTls){.alloc = alloc};
  if (UNLIKELY(!g_netSchannelReady)) {
    tls->status = NetResult_TlsUnavailable;
    return tls;
  }

  // TODO: Implement.
  tls->status = NetResult_TlsUnavailable;
  return tls;
}

void net_tls_destroy(NetTls* tls) { alloc_free_t(tls->alloc, tls); }

NetResult net_tls_status(const NetTls* tls) { return tls->status; }

NetResult net_tls_write_sync(NetTls* tls, NetSocket* socket, const String data) {
  if (tls->status != NetResult_Success) {
    return tls->status;
  }
  diag_assert(g_netSchannelReady);

  // TODO: Implement.
  (void)socket;
  (void)data;
  return NetResult_TlsUnavailable;
}

NetResult net_tls_read_sync(NetTls* tls, NetSocket* socket, DynString* out) {
  if (tls->status != NetResult_Success) {
    return tls->status;
  }
  diag_assert(g_netSchannelReady);

  // TODO: Implement.
  (void)socket;
  (void)out;
  return NetResult_TlsUnavailable;
}

NetResult net_tls_shutdown_sync(NetTls* tls, NetSocket* socket) {
  if (tls->status != NetResult_Success && tls->status != NetResult_TlsClosed) {
    return NetResult_Success; // Session failed, no need to shutdown.
  }
  diag_assert(g_netSchannelReady);

  // TODO: Implement.
  (void)socket;
  return NetResult_TlsUnavailable;
}
