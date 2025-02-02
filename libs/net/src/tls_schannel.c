#include "core_alloc.h"
#include "core_array.h"
#include "core_diag.h"
#include "core_dynstring.h"
#include "core_winutils.h"
#include "log_logger.h"
#include "net_result.h"
#include "net_socket.h"
#include "net_tls.h"

#include "tls_internal.h"

#include <windows.h>

#define SECURITY_WIN32
#include <schannel.h>
#include <security.h>
#include <shlwapi.h> // TODO: Verify if needed.

/**
 * TODO:
 */

#pragma comment(lib, "secur32.lib")
#pragma comment(lib, "shlwapi.lib")

typedef struct {
  CredHandle credHandle;
} NetSchannel;

static SEC_WCHAR* to_sec_null_term_scratch(const String str) {
  if (string_is_empty(str)) {
    return null;
  }
  return (SEC_WCHAR*)winutils_to_widestr_scratch(str).ptr;
}

static bool net_schannel_init(NetSchannel* schannel, Allocator* alloc) {
  (void)alloc;

  SCHANNEL_CRED credCfg = {
      .dwVersion             = SCHANNEL_CRED_VERSION,
      .grbitEnabledProtocols = 0, // Let the system decide.
      .dwFlags               = SCH_USE_STRONG_CRYPTO | SCH_CRED_NO_DEFAULT_CREDS,
  };
  if (AcquireCredentialsHandle(
          null,
          UNISP_NAME,
          SECPKG_CRED_OUTBOUND,
          null,
          &credCfg,
          null,
          null,
          &schannel->credHandle,
          null) != SEC_E_OK) {
    log_w("Tls: Schannel failed to acquire credentials");
    return false;
  }

  log_i("Tls: Schannel initialized");
  return true;
}

static NetSchannel g_netSchannel;
static bool        g_netSchannelReady;

void net_tls_init(void) {
  diag_assert(!g_netSchannelReady);
  g_netSchannelReady = net_schannel_init(&g_netSchannel, g_allocPersist);
}

void net_tls_teardown(void) {
  if (g_netSchannelReady) {
    FreeCredentialsHandle(&g_netSchannel.credHandle);
  }
  g_netSchannel      = (NetSchannel){0};
  g_netSchannelReady = false;
}

typedef struct sNetTls {
  Allocator*                alloc;
  NetResult                 status;
  String                    host;
  bool                      connected;
  CtxtHandle                context;
  bool                      contextCreated;
  SecPkgContext_StreamSizes sizes;
  DynString                 readBuffer;
} NetTls;

static NetResult net_tls_write_buffer_sync(const SecBuffer buffer, NetSocket* socket) {
  if (!buffer.cbBuffer) {
    return NetResult_Success; // Nothing to send.
  }
  return net_socket_write_sync(socket, mem_create(buffer.pvBuffer, buffer.cbBuffer));
}

static String net_tls_schannel_error_msg(const LONG err) {
  // clang-format off
  switch (err) {
  case SEC_E_INSUFFICIENT_MEMORY:           return string_lit("INSUFFICIENT_MEMORY");
  case SEC_E_INVALID_TOKEN:                 return string_lit("INVALID_TOKEN");
  case SEC_E_LOGON_DENIED:                  return string_lit("LOGON_DENIED");
  case SEC_E_NO_AUTHENTICATING_AUTHORITY:   return string_lit("NO_AUTHENTICATING_AUTHORITY");
  case SEC_E_NO_CREDENTIALS:                return string_lit("NO_CREDENTIALS");
  case SEC_E_TARGET_UNKNOWN:                return string_lit("TARGET_UNKNOWN");
  case SEC_E_WRONG_PRINCIPAL:               return string_lit("WRONG_PRINCIPAL");
  case SEC_E_APPLICATION_PROTOCOL_MISMATCH: return string_lit("APPLICATION_PROTOCOL_MISMATCH");
  default:                                  return string_lit("UNKNOWN");
  }
  // clang-format on
}

static void net_tls_connect_sync(NetTls* tls, NetSocket* socket) {
  CtxtHandle* currentCtx = null;
  for (;;) {
    const String        dataIn      = dynstring_view(&tls->readBuffer);
    const unsigned long dataInSize  = (unsigned long)dataIn.size;
    SecBuffer           buffersIn[] = {
        [0] = {.BufferType = SECBUFFER_TOKEN, .pvBuffer = dataIn.ptr, .cbBuffer = dataInSize},
        [1] = {.BufferType = SECBUFFER_EMPTY},
    };
    SecBuffer buffersOut[] = {
        [0] = {.BufferType = SECBUFFER_TOKEN},
    };
    SecBufferDesc descIn  = {SECBUFFER_VERSION, array_elems(buffersIn), buffersIn};
    SecBufferDesc descOut = {SECBUFFER_VERSION, array_elems(buffersOut), buffersOut};

    DWORD initFlags = ISC_REQ_ALLOCATE_MEMORY | ISC_REQ_CONFIDENTIALITY | ISC_REQ_REPLAY_DETECT |
                      ISC_REQ_SEQUENCE_DETECT | ISC_REQ_STREAM | ISC_REQ_USE_SUPPLIED_CREDS;

    const SECURITY_STATUS initStatus = InitializeSecurityContext(
        &g_netSchannel.credHandle,
        currentCtx,
        currentCtx ? null : to_sec_null_term_scratch(tls->host),
        initFlags,
        0,
        0,
        currentCtx ? &descIn : null,
        0,
        currentCtx ? null : &tls->context,
        &descOut,
        &initFlags,
        null);

    // Context now available for re-use.
    currentCtx          = &tls->context;
    tls->contextCreated = true;

    // Consume the read data.
    if (buffersIn[1].BufferType == SECBUFFER_EXTRA) {
      diag_assert(tls->readBuffer.size >= buffersIn[1].cbBuffer);
      dynstring_erase_chars(&tls->readBuffer, 0, tls->readBuffer.size - buffersIn[1].cbBuffer);
      diag_assert(tls->readBuffer.size == buffersIn[1].cbBuffer);
    } else if (buffersIn[1].BufferType != SECBUFFER_MISSING) {
      dynstring_clear(&tls->readBuffer); // Schannel consumed all the data.
    }

    // Send data to remote.
    if (buffersOut[0].pvBuffer) {
      tls->status = net_tls_write_buffer_sync(buffersOut[0], socket);
      FreeContextBuffer(buffersOut[0].pvBuffer);
      if (tls->status != NetResult_Success) {
        return;
      }
    }

    switch (initStatus) {
    case SEC_E_OK:
      goto Success; // Connection established.
    case SEC_I_INCOMPLETE_CREDENTIALS:
      tls->status = NetResult_TlsCredentialsRequired; // Client certification is not supported.
      return;
    case SEC_I_CONTINUE_NEEDED:
      break;
    case SEC_E_INCOMPLETE_MESSAGE:
      tls->status = net_socket_read_sync(socket, &tls->readBuffer);
      if (tls->status != NetResult_Success) {
        return;
      }
      break;
    default:
      log_e(
          "SChannel connect failed",
          log_param("msg", fmt_text(net_tls_schannel_error_msg(initStatus))),
          log_param("code", fmt_int((u32)initStatus)));
      tls->status = NetResult_TlsFailed;
      return;
    }
  }

Success:
  QueryContextAttributes(&tls->context, SECPKG_ATTR_STREAM_SIZES, &tls->sizes);
}

NetTls* net_tls_create(Allocator* alloc, const String host, const NetTlsFlags flags) {
  NetTls* tls = alloc_alloc_t(alloc, NetTls);

  *tls = (NetTls){
      .alloc      = alloc,
      .host       = string_maybe_dup(alloc, host),
      .readBuffer = dynstring_create(g_allocHeap, usize_kibibyte * 16),
  };
  if (UNLIKELY(!g_netSchannelReady)) {
    tls->status = NetResult_TlsUnavailable;
    return tls;
  }
  // NOTE: Connect will be performed on the first read / write.
  return tls;
}

void net_tls_destroy(NetTls* tls) {
  if (tls->contextCreated) {
    DeleteSecurityContext(&tls->context);
  }
  string_maybe_free(tls->alloc, tls->host);
  dynstring_destroy(&tls->readBuffer);
  alloc_free_t(tls->alloc, tls);
}

NetResult net_tls_status(const NetTls* tls) { return tls->status; }

NetResult net_tls_write_sync(NetTls* tls, NetSocket* socket, const String data) {
  if (tls->status != NetResult_Success) {
    return tls->status;
  }
  diag_assert(g_netSchannelReady);

  if (!tls->connected) {
    net_tls_connect_sync(tls, socket);
    if (tls->status != NetResult_Success) {
      return tls->status;
    }
    tls->connected = true;
  }

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

  if (!tls->connected) {
    net_tls_connect_sync(tls, socket);
    if (tls->status != NetResult_Success) {
      return tls->status;
    }
    tls->connected = true;
  }

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
