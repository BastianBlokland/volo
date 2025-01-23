#include "core_alloc.h"
#include "core_diag.h"
#include "core_dynlib.h"
#include "core_dynstring.h"
#include "core_env.h"
#include "core_path.h"
#include "log_logger.h"
#include "net_result.h"
#include "net_socket.h"
#include "net_tls.h"

#include "tls_internal.h"

/**
 * Tls client implementation using the OpenSSL (libssl) library.
 * Website: https://www.openssl.org/
 * Documentation: https://docs.openssl.org/3.0/man3/
 *
 * Simple synchonous implementation that uses memory-bio's to read / write data from / to OpenSSL.
 */

#define net_tls_openssl_names_max 4

#define SSL_VERIFY_NONE 0x00
#define SSL_VERIFY_PEER 0x01
#define SSL_CTRL_MODE 33
#define SSL_MODE_ENABLE_PARTIAL_WRITE 0x00000001U
#define SSL_ERROR_WANT_READ 2
#define SSL_ERROR_WANT_WRITE 3
#define SSL_ERROR_ZERO_RETURN 6

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
  long              (SYS_DECL* SSL_CTX_ctrl)(SSL_CTX*, int cmd, long larg, void* parg);
  SSL*              (SYS_DECL* SSL_new)(SSL_CTX*);
  void              (SYS_DECL* SSL_free)(SSL*);
  int               (SYS_DECL* SSL_shutdown)(SSL*);
  void              (SYS_DECL* SSL_set_verify)(SSL*, int mode, const void* callback);
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

static u32 net_openssl_lib_names(String outPaths[PARAM_ARRAY_SIZE(net_tls_openssl_names_max)]) {
  const String openSslPath = env_var_scratch(string_lit("OPENSSL_BIN"));

  u32 count = 0;
#if defined(VOLO_WIN32)
  outPaths[count++] = string_lit("libssl-3-x64.dll");
  if (!string_is_empty(openSslPath)) {
    outPaths[count++] = path_build_scratch(openSslPath, string_lit("libssl-3-x64.dll"));
  }
  outPaths[count++] = string_lit("libssl-1_1-x64.dll");
  if (!string_is_empty(openSslPath)) {
    outPaths[count++] = path_build_scratch(openSslPath, string_lit("libssl-1_1-x64.dll"));
  }
#else
  outPaths[count++] = string_lit("libssl.so");
  if (!string_is_empty(openSslPath)) {
    outPaths[count++] = path_build_scratch(openSslPath, string_lit("libssl.so"));
  }
#endif
  return count;
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
  String    libNames[net_tls_openssl_names_max];
  const u32 libNameCount = net_openssl_lib_names(libNames);

  const DynLibResult loadRes = dynlib_load_first(alloc, libNames, libNameCount, &ssl->lib);
  if (UNLIKELY(loadRes != DynLibResult_Success)) {
    const String err = dynlib_result_str(loadRes);
    log_w("Failed to load OpenSSL library", log_param("err", fmt_text(err)));
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
  OPENSSL_LOAD_SYM(SSL_CTX_ctrl);
  OPENSSL_LOAD_SYM(SSL_new);
  OPENSSL_LOAD_SYM(SSL_free);
  OPENSSL_LOAD_SYM(SSL_shutdown);
  OPENSSL_LOAD_SYM(SSL_set_verify);
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

  /**
   * TODO: Replace this with a scratch buffer. Currently it has to be stored on the heap because
   * theoretically there is no limit to the amount of data in the socket buffer. To fix this we need
   * to add a new parameter to 'net_socket_read_sync' that specifies the maximum amount to read.
   */
  DynString readBuffer;
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
  Mem buffer = alloc_alloc(g_allocScratch, usize_kibibyte * 32, 1);
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

NetTls* net_tls_create(Allocator* alloc, const NetTlsFlags flags) {
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
  if (flags & NetTlsFlags_NoVerify) {
    g_netOpenSslLib.SSL_set_verify(tls->session, SSL_VERIFY_NONE, null /* callback */);
  } else {
    g_netOpenSslLib.SSL_set_verify(tls->session, SSL_VERIFY_PEER, null /* callback */);
  }

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

  for (u8* itr = mem_begin(data); itr != mem_end(data);) {
    // Write the raw data to OpenSSL to be encrypted.
    size_t    bytesWritten = 0;
    const int ret = g_netOpenSslLib.SSL_write_ex(tls->session, data.ptr, data.size, &bytesWritten);
    const int err = g_netOpenSslLib.SSL_get_error(tls->session, ret);

    // Write the encrypted data to the socket.
    tls->status = net_tls_write_ouput_sync(tls, socket);
    if (tls->status != NetResult_Success) {
      return tls->status;
    }
    if (ret > 0) {
      // Chunk of payload has been written to the socket; continue to the next chunk.
      itr += bytesWritten;
      continue;
    }
    // No payload was written, either an error occurred or OpenSSL wants to read data (for example
    // for a handshake) from the socket.
    switch (err) {
    case SSL_ERROR_WANT_READ:
      tls->status = net_tls_read_input_sync(tls, socket);
      if (tls->status != NetResult_Success) {
        return tls->status;
      }
      continue; // New input was read; retry the OpenSSL write.
    case SSL_ERROR_WANT_WRITE:
      continue; // Output was already written to the socket; retry the OpenSSL write.
    case SSL_ERROR_ZERO_RETURN:
      return tls->status = NetResult_TlsClosed;
    default:
      net_openssl_handle_errors(&g_netOpenSslLib);
      return tls->status = NetResult_TlsFailed;
    }
  }
  return NetResult_Success;
}

NetResult net_tls_read_sync(NetTls* tls, NetSocket* socket, DynString* out) {
  if (tls->status != NetResult_Success) {
    return tls->status;
  }
  diag_assert(g_netOpenSslReady && tls->session);

  Mem   buffer         = alloc_alloc(g_allocScratch, usize_kibibyte * 32, 1);
  usize totalBytesRead = 0;
  for (;;) {
    // Ask OpenSSL to decrypt data buffered append it to the output.
    size_t    bytesRead;
    const int ret = g_netOpenSslLib.SSL_read_ex(tls->session, buffer.ptr, buffer.size, &bytesRead);
    const int err = g_netOpenSslLib.SSL_get_error(tls->session, ret);

    // Write any control data to the socket, can happen if OpenSSL is performing the handshake.
    tls->status = net_tls_write_ouput_sync(tls, socket);
    if (tls->status != NetResult_Success) {
      return tls->status;
    }

    if (ret > 0) {
      // Chunk of payload has been decrypted; append it to the output.
      diag_assert(bytesRead);
      totalBytesRead += bytesRead;
      dynstring_append(out, string_slice(buffer, 0, bytesRead));
      continue;
    }

    // No payload could be decrypted, either an error ocurred or we need more data from the socket.
    switch (err) {
    case SSL_ERROR_WANT_READ:
      if (totalBytesRead) {
        return NetResult_Success; // We've successfully read all the available data.
      }
      tls->status = net_tls_read_input_sync(tls, socket);
      if (tls->status != NetResult_Success) {
        return tls->status;
      }
      continue; // New input was read; retry the OpenSSL write.
    case SSL_ERROR_WANT_WRITE:
      continue; // Output was already written to the socket; retry the OpenSSL read.
    case SSL_ERROR_ZERO_RETURN:
      return tls->status = NetResult_TlsClosed;
    default:
      net_openssl_handle_errors(&g_netOpenSslLib);
      return tls->status = NetResult_TlsFailed;
    }
  }
}

NetResult net_tls_shutdown_sync(NetTls* tls, NetSocket* socket) {
  if (tls->status != NetResult_Success && tls->status != NetResult_TlsClosed) {
    return tls->status;
  }
  diag_assert(g_netOpenSslReady && tls->session);

  // Ask OpenSSL to shutdown the connection.
  g_netOpenSslLib.SSL_shutdown(tls->session);

  // Wait for the connection to be closed.
  for (;;) {
    const int ret = g_netOpenSslLib.SSL_read_ex(tls->session, null, 0, 0);
    const int err = g_netOpenSslLib.SSL_get_error(tls->session, ret);

    // Write any output to the socket (OpenSSL needs to send the close_notify alert message).
    tls->status = net_tls_write_ouput_sync(tls, socket);
    if (tls->status != NetResult_Success) {
      return tls->status; // Shutdown failed.
    }

    switch (err) {
    case SSL_ERROR_WANT_READ:
      tls->status = net_tls_read_input_sync(tls, socket);
      if (tls->status == NetResult_ConnectionClosed) {
        return NetResult_Success; // Shutdown successful. Remote closed the socket.
      }
      if (tls->status != NetResult_Success) {
        return tls->status; // Shutdown failed.
      }
      continue; // New input was read; retry the OpenSSL shutdown.
    case SSL_ERROR_WANT_WRITE:
      continue; // Output was already written to the socket; retry the OpenSSL shutdown.
    case SSL_ERROR_ZERO_RETURN:
      tls->status = NetResult_TlsClosed;
      return NetResult_Success; // Shutdown successful.
    default:
      net_openssl_handle_errors(&g_netOpenSslLib);
      return tls->status = NetResult_TlsFailed;
    }
  }
}
