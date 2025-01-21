#include "core_alloc.h"
#include "core_diag.h"
#include "core_dynlib.h"
#include "core_dynstring.h"
#include "log_logger.h"
#include "net_result.h"
#include "net_socket.h"
#include "net_tls.h"

#include "tls_internal.h"

#define SSL_VERIFY_PEER 0x01
#define SSL_CTRL_MODE 33
#define SSL_MODE_ENABLE_PARTIAL_WRITE 0x00000001U
#define SSL_ERROR_WANT_READ 2
#define SSL_ERROR_WANT_WRITE 3

typedef struct sSSL        SSL;
typedef struct sSSL_METHOD SSL_METHOD;
typedef struct sSSL_CTX    SSL_CTX;
typedef struct sBIO        BIO;
typedef struct sBIO_METHOD BIO_METHOD;

typedef struct {
  DynLib* lib;
  // clang-format off
  int               (SYS_DECL* OPENSSL_init_ssl)(u64 opts, const void* settings);
  unsigned long     (SYS_DECL* ERR_get_error)(void);
  void              (SYS_DECL* ERR_error_string_n)(unsigned long e, char *buf, size_t len);
  const SSL_METHOD* (SYS_DECL* TLS_client_method)(void);
  SSL_CTX*          (SYS_DECL* SSL_CTX_new)(const SSL_METHOD*);
  void              (SYS_DECL* SSL_CTX_free)(SSL_CTX*);
  void              (SYS_DECL* SSL_CTX_set_verify)(SSL_CTX*, int mode, const void* callback);
  long              (SYS_DECL* SSL_CTX_ctrl)(SSL_CTX*, int cmd, long larg, void* parg);
  SSL*              (SYS_DECL* SSL_new)(SSL_CTX*);
  void              (SYS_DECL* SSL_free)(SSL*);
  void              (SYS_DECL* SSL_set_connect_state)(SSL*);
  void              (SYS_DECL* SSL_set_bio)(SSL*, BIO* readBio, BIO* writeBio);
  int               (SYS_DECL* SSL_read_ex)(SSL*, void *buf, size_t num, size_t *readBytes);
  int               (SYS_DECL* SSL_write_ex)(SSL*, const void *buf, size_t num, size_t *written);
  int               (SYS_DECL* SSL_get_error)(const SSL*, int ret);
  BIO_METHOD*       (SYS_DECL* BIO_s_mem)(void);
  BIO*              (SYS_DECL* BIO_new)(const BIO_METHOD*);
  void              (SYS_DECL* BIO_free_all)(BIO*);
  int               (SYS_DECL* BIO_read_ex)(BIO*, void *data, size_t dlen, size_t *readbytes);
  int               (SYS_DECL* BIO_write_ex)(BIO*, const void *data, size_t dlen, size_t *written);
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

static void net_openssl_handle_errors(NetOpenSsl* ssl) {
  char buffer[256];
  for (;;) {
    const unsigned long err = ssl->ERR_get_error();
    if (!err) {
      break;
    }
    ssl->ERR_error_string_n(err, buffer, sizeof(buffer));
    const String msg = string_from_null_term(buffer);
    log_e("OpenSSL {}", log_param("msg", fmt_text(msg)), log_param("code", fmt_int((u64)err)));
  }
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
    if (UNLIKELY(!ssl->_NAME_)) {                                                                  \
      log_w("OpenSSL symbol '{}' missing", log_param("sym", fmt_text(string_lit(#_NAME_))));       \
      return false;                                                                                \
    }                                                                                              \
  } while (false)

  OPENSSL_LOAD_SYM(OPENSSL_init_ssl);
  OPENSSL_LOAD_SYM(ERR_get_error);
  OPENSSL_LOAD_SYM(ERR_error_string_n);
  OPENSSL_LOAD_SYM(TLS_client_method);
  OPENSSL_LOAD_SYM(SSL_CTX_new);
  OPENSSL_LOAD_SYM(SSL_CTX_free);
  OPENSSL_LOAD_SYM(SSL_CTX_set_verify);
  OPENSSL_LOAD_SYM(SSL_CTX_ctrl);
  OPENSSL_LOAD_SYM(SSL_new);
  OPENSSL_LOAD_SYM(SSL_free);
  OPENSSL_LOAD_SYM(SSL_set_connect_state);
  OPENSSL_LOAD_SYM(SSL_set_bio);
  OPENSSL_LOAD_SYM(SSL_read_ex);
  OPENSSL_LOAD_SYM(SSL_write_ex);
  OPENSSL_LOAD_SYM(SSL_get_error);
  OPENSSL_LOAD_SYM(BIO_s_mem);
  OPENSSL_LOAD_SYM(BIO_new);
  OPENSSL_LOAD_SYM(BIO_free_all);
  OPENSSL_LOAD_SYM(BIO_read_ex);
  OPENSSL_LOAD_SYM(BIO_write_ex);

  if (UNLIKELY(!ssl->OPENSSL_init_ssl(0 /* options */, null /* settings */))) {
    net_openssl_handle_errors(ssl);
    return false;
  }
  if (UNLIKELY(!(ssl->clientContext = ssl->SSL_CTX_new(ssl->TLS_client_method())))) {
    net_openssl_handle_errors(ssl);
    return false;
  }
  ssl->SSL_CTX_set_verify(ssl->clientContext, SSL_VERIFY_PEER, null /* callback */);
  ssl->SSL_CTX_ctrl(ssl->clientContext, SSL_CTRL_MODE, SSL_MODE_ENABLE_PARTIAL_WRITE, null);

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
  NetResult  status;
  SSL*       session;
  BIO*       input;
  BIO*       output;
  DynString  readBuffer;
} NetTls;

static NetResult net_tls_read_input_sync(NetTls* tls, NetSocket* socket) {
  const NetResult socketResult = net_socket_read_sync(socket, &tls->readBuffer);
  if (socketResult != NetResult_Success) {
    return socketResult;
  }
  const String data = dynstring_view(&tls->readBuffer);

  size_t    bytesWritten;
  const int res = g_netOpenSslLib.BIO_write_ex(tls->input, data.ptr, data.size, &bytesWritten);

  dynstring_clear(&tls->readBuffer);
  return (res && bytesWritten == data.size) ? NetResult_Success : NetResult_TlsBufferExhausted;
}

static NetResult net_tls_write_ouput_sync(NetTls* tls, NetSocket* socket) {
  Mem buffer = mem_stack(usize_kibibyte * 16);
  for (;;) {
    usize bytesRead;
    if (!g_netOpenSslLib.BIO_read_ex(tls->output, buffer.ptr, buffer.size, &bytesRead)) {
      return NetResult_Success; // Nothing to write.
    }
    if (!bytesRead) {
      return NetResult_Success; // Nothing to write.
    }
    const NetResult socketRes = net_socket_write_sync(socket, mem_slice(buffer, 0, bytesRead));
    if (socketRes != NetResult_Success) {
      return socketRes;
    }
  }
}

NetTls* net_tls_create(Allocator* alloc) {
  NetTls* tls = alloc_alloc_t(alloc, NetTls);

  *tls = (NetTls){.alloc = alloc, .readBuffer = dynstring_create(g_allocHeap, usize_kibibyte * 16)};
  if (UNLIKELY(!g_netOpenSslReady)) {
    tls->status = NetResult_TlsUnavailable;
    return tls;
  }

  // Create a session.
  if (UNLIKELY(!(tls->session = g_netOpenSslLib.SSL_new(g_netOpenSslLib.clientContext)))) {
    net_openssl_handle_errors(&g_netOpenSslLib);
    tls->status = NetResult_TlsUnavailable;
    return tls;
  }
  g_netOpenSslLib.SSL_set_connect_state(tls->session); // Client mode.

  // Setup memory bio's.
  tls->input  = g_netOpenSslLib.BIO_new(g_netOpenSslLib.BIO_s_mem());
  tls->output = g_netOpenSslLib.BIO_new(g_netOpenSslLib.BIO_s_mem());
  if (UNLIKELY(!tls->input || !tls->output)) {
    net_openssl_handle_errors(&g_netOpenSslLib);
    if (tls->input) {
      g_netOpenSslLib.BIO_free_all(tls->input);
    }
    if (tls->output) {
      g_netOpenSslLib.BIO_free_all(tls->output);
    }
    tls->status = NetResult_TlsUnavailable;
    return tls;
  }
  g_netOpenSslLib.SSL_set_bio(tls->session, tls->input, tls->output);

  return tls;
}

void net_tls_destroy(NetTls* tls) {
  if (tls->session) {
    g_netOpenSslLib.SSL_free(tls->session);
  }
  dynstring_destroy(&tls->readBuffer);
  alloc_free_t(tls->alloc, tls);
}

NetResult net_tls_status(const NetTls* tls) { return tls->status; }

NetResult net_tls_write_sync(NetTls* tls, NetSocket* socket, const String data) {
  if (tls->status != NetResult_Success) {
    return tls->status;
  }
  diag_assert(g_netOpenSslReady && tls->session);

  // Write the raw data to OpenSSL to be encrypted.
  // NOTE: Can cause socket reads if the handshake hasn't completed.
  for (u8* itr = mem_begin(data); itr != mem_end(data);) {
    size_t    bytesWritten = 0;
    const int ret = g_netOpenSslLib.SSL_write_ex(tls->session, data.ptr, data.size, &bytesWritten);
    if (ret > 0) {
      itr += bytesWritten;
      continue;
    }
    const int err = g_netOpenSslLib.SSL_get_error(tls->session, ret);
    switch (err) {
    case SSL_ERROR_WANT_READ:
      tls->status = net_tls_read_input_sync(tls, socket);
      if (tls->status != NetResult_Success) {
        return tls->status;
      }
      continue; // Retry.
    case SSL_ERROR_WANT_WRITE:
      tls->status = net_tls_write_ouput_sync(tls, socket);
      if (tls->status != NetResult_Success) {
        return tls->status;
      }
      continue; // Retry.
    default:
      net_openssl_handle_errors(&g_netOpenSslLib);
      return tls->status = NetResult_TlsFailed;
    }
  }

  // Write the encrypted data to the socket.
  return net_tls_write_ouput_sync(tls, socket);
}

NetResult net_tls_read_sync(NetTls* tls, NetSocket* socket, DynString* out) {
  if (tls->status != NetResult_Success) {
    return tls->status;
  }
  diag_assert(g_netOpenSslReady && tls->session);

  // Ask OpenSSL to decrypt data from the socket and write it to the output.
  // NOTE: Can cause socket writes if the handshake hasn't completed.
  Mem   buffer         = mem_stack(usize_kibibyte * 16);
  usize totalBytesRead = 0;
  for (;;) {
    size_t    bytesRead;
    const int ret = g_netOpenSslLib.SSL_read_ex(tls->session, buffer.ptr, buffer.size, &bytesRead);
    if (ret > 0) {
      diag_assert(bytesRead);
      totalBytesRead += bytesRead;
      dynstring_append(out, string_slice(buffer, 0, bytesRead));
      continue;
    }
    const int err = g_netOpenSslLib.SSL_get_error(tls->session, ret);
    switch (err) {
    case SSL_ERROR_WANT_READ:
      if (totalBytesRead) {
        return NetResult_Success; // We've successfully read all the available data.
      }
      tls->status = net_tls_read_input_sync(tls, socket);
      if (tls->status != NetResult_Success) {
        return tls->status;
      }
      continue; // Retry.
    case SSL_ERROR_WANT_WRITE:
      tls->status = net_tls_write_ouput_sync(tls, socket);
      if (tls->status != NetResult_Success) {
        return tls->status;
      }
      continue; // Retry.
    default:
      net_openssl_handle_errors(&g_netOpenSslLib);
      return tls->status = NetResult_TlsFailed;
    }
  }
}
