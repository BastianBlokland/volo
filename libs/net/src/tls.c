#include "core_alloc.h"
#include "core_diag.h"
#include "core_dynlib.h"
#include "log_logger.h"
#include "net_result.h"
#include "net_tls.h"

#include "tls_internal.h"

typedef struct sSSL_METHOD SSL_METHOD;
typedef struct sSSL_CTX    SSL_CTX;

typedef struct {
  DynLib* lib;
  // clang-format off
  int               (SYS_DECL* OPENSSL_init_ssl)(u64 opts, const void* settings);
  const SSL_METHOD* (SYS_DECL* TLS_client_method)(void);
  SSL_CTX*          (SYS_DECL* SSL_CTX_new)(const SSL_METHOD*);
  void              (SYS_DECL* SSL_CTX_free)(SSL_CTX*);
  // clang-format on

  SSL_CTX* clientContext;
} NetOpenSsl;

static String net_openssl_search_path(void) {
#if defined(VOLO_WIN32)
  return string_lit("libssl.dll");
#else
  return string_lit("libssl.so");
#endif
}

static bool net_openssl_init(NetOpenSsl* ssl, Allocator* alloc) {
  const String       searchPath = net_openssl_search_path();
  const DynLibResult loadRes    = dynlib_load(alloc, searchPath, &ssl->lib);
  if (UNLIKELY(loadRes != DynLibResult_Success)) {
    const String err = dynlib_result_str(loadRes);
    log_w(
        "Failed to load OpenSSL library ({})",
        log_param("path", fmt_text(searchPath)),
        log_param("err", fmt_text(err)));
    return false;
  }

#define OPENSSL_LOAD_SYM(_NAME_)                                                                   \
  do {                                                                                             \
    ssl->_NAME_ = dynlib_symbol(ssl->lib, string_lit(#_NAME_));                                    \
    if (!ssl->_NAME_) {                                                                            \
      log_w("OpenSSL symbol '{}' missing", log_param("sym", fmt_text(string_lit(#_NAME_))));       \
      return false;                                                                                \
    }                                                                                              \
  } while (false)

  OPENSSL_LOAD_SYM(OPENSSL_init_ssl);
  OPENSSL_LOAD_SYM(TLS_client_method);
  OPENSSL_LOAD_SYM(SSL_CTX_new);
  OPENSSL_LOAD_SYM(SSL_CTX_free);

  if (!ssl->OPENSSL_init_ssl(0 /* options */, null /* settings */)) {
    log_e("OpenSSL init failed");
    return false;
  }

  if (!(ssl->clientContext = ssl->SSL_CTX_new(ssl->TLS_client_method()))) {
    log_e("OpenSSL failed to create client-context");
    return false;
  }

  log_i("OpenSSL library loaded", log_param("path", fmt_path(dynlib_path(ssl->lib))));

#undef OPENSSL_LOAD_SYM
  return true;
}

static NetOpenSsl g_netOpenSslLib;
static bool       g_netOpenSslReady;

void net_tls_init(void) {
  diag_assert(!g_netOpenSslReady);
  g_netOpenSslReady = net_openssl_init(&g_netOpenSslLib, g_allocPersist);
}

void net_tls_teardown(void) {
  if (g_netOpenSslLib.clientContext) {
    g_netOpenSslLib.SSL_CTX_free(g_netOpenSslLib.clientContext);
  }
  if (g_netOpenSslLib.lib) {
    dynlib_destroy(g_netOpenSslLib.lib);
  }
  g_netOpenSslLib   = (NetOpenSsl){0};
  g_netOpenSslReady = false;
}

typedef struct sNetTls {
  Allocator* alloc;
  NetSocket* socket;
  NetResult  status;
} NetTls;

NetTls* net_tls_connect_sync(Allocator* alloc, NetSocket* socket) {
  (void)socket;

  NetTls* tls = alloc_alloc_t(alloc, NetTls);

  *tls = (NetTls){.alloc = alloc, .socket = socket};
  if (UNLIKELY(!g_netOpenSslReady)) {
    tls->status = NetResult_TlsUnavailable;
    return tls;
  }

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
