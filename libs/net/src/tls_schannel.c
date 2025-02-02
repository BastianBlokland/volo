#include "core_alloc.h"
#include "core_array.h"
#include "core_bits.h"
#include "core_diag.h"
#include "core_dynlib.h"
#include "core_dynstring.h"
#include "core_math.h"
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

/**
 * Tls (Transport Layer Security) client implementation using the 'Secure Channel' Windows api.
 * 'Secure Channel' is part of the SSPI 'Security Support Provider Interface' Windows component.
 * Documentation: https://learn.microsoft.com/en-us/windows/win32/secauthn/secure-channel
 *
 * NOTE: Renegotiations are not supported at this time.
 */

typedef struct {
  DynLib* lib;

  ACQUIRE_CREDENTIALS_HANDLE_FN_W  AcquireCredentialsHandleW;
  FREE_CREDENTIALS_HANDLE_FN       FreeCredentialsHandle;
  INITIALIZE_SECURITY_CONTEXT_FN_W InitializeSecurityContextW;
  DELETE_SECURITY_CONTEXT_FN       DeleteSecurityContext;
  APPLY_CONTROL_TOKEN_FN           ApplyControlToken;
  QUERY_CONTEXT_ATTRIBUTES_FN_W    QueryContextAttributesW;
  FREE_CONTEXT_BUFFER_FN           FreeContextBuffer;
  ENCRYPT_MESSAGE_FN               EncryptMessage;
  DECRYPT_MESSAGE_FN               DecryptMessage;

  CredHandle creds, credsNoVerify;
} NetSChannel;

static SEC_WCHAR* to_sec_null_term_scratch(const String str) {
  if (string_is_empty(str)) {
    return null;
  }
  return (SEC_WCHAR*)winutils_to_widestr_scratch(str).ptr;
}

static bool net_schannel_create_cred(NetSChannel* schannel, const bool noVerify, CredHandle* out) {
  DWORD flags = SCH_CRED_NO_DEFAULT_CREDS;
#ifdef SCH_USE_STRONG_CRYPTO
  flags |= SCH_USE_STRONG_CRYPTO;
#endif
  if (noVerify) {
    flags |= SCH_CRED_MANUAL_CRED_VALIDATION;
  }

  SCHANNEL_CRED credCfg = {
      .dwVersion             = SCHANNEL_CRED_VERSION,
      .grbitEnabledProtocols = 0, // Let the system decide.
      .dwFlags               = flags,
  };

  const SECURITY_STATUS credStatus = schannel->AcquireCredentialsHandleW(
      null, UNISP_NAME, SECPKG_CRED_OUTBOUND, null, &credCfg, null, null, out, null);

  if (credStatus != SEC_E_OK) {
    log_w("SChannel failed to acquire credentials");
    return false;
  }
  return true;
}

static bool net_schannel_init(NetSChannel* schannel, Allocator* alloc) {
  const DynLibResult loadRes = dynlib_load(alloc, string_lit("secur32.dll"), &schannel->lib);
  if (UNLIKELY(loadRes != DynLibResult_Success)) {
    const String err = dynlib_result_str(loadRes);
    log_w("Failed to load Secur32 library ('secur32.dll')", log_param("err", fmt_text(err)));
    return false;
  }

#define SECUR_LOAD_SYM(_NAME_)                                                                     \
  do {                                                                                             \
    schannel->_NAME_ = dynlib_symbol(schannel->lib, string_lit(#_NAME_));                          \
    if (!schannel->_NAME_) {                                                                       \
      log_w("Secur32 symbol '{}' missing", log_param("sym", fmt_text(string_lit(#_NAME_))));       \
      return false;                                                                                \
    }                                                                                              \
  } while (false)

  SECUR_LOAD_SYM(AcquireCredentialsHandleW);
  SECUR_LOAD_SYM(FreeCredentialsHandle);
  SECUR_LOAD_SYM(InitializeSecurityContextW);
  SECUR_LOAD_SYM(DeleteSecurityContext);
  SECUR_LOAD_SYM(ApplyControlToken);
  SECUR_LOAD_SYM(QueryContextAttributesW);
  SECUR_LOAD_SYM(FreeContextBuffer);
  SECUR_LOAD_SYM(EncryptMessage);
  SECUR_LOAD_SYM(DecryptMessage);

  if (!net_schannel_create_cred(schannel, false /* noVerify */, &schannel->creds)) {
    return false;
  }
  if (!net_schannel_create_cred(schannel, true /* noVerify */, &schannel->credsNoVerify)) {
    return false;
  }

  log_i("SChannel initialized", log_param("path", fmt_path(dynlib_path(schannel->lib))));
  return true;
}

static NetSChannel g_netSChannel;
static bool        g_netSChannelReady;

void net_tls_init(void) {
  diag_assert(!g_netSChannelReady);
  g_netSChannelReady = net_schannel_init(&g_netSChannel, g_allocPersist);
}

void net_tls_teardown(void) {
  if (g_netSChannelReady) {
    g_netSChannel.FreeCredentialsHandle(&g_netSChannel.creds);
    g_netSChannel.FreeCredentialsHandle(&g_netSChannel.credsNoVerify);
  }
  if (g_netSChannel.lib) {
    dynlib_destroy(g_netSChannel.lib);
  }
  g_netSChannel      = (NetSChannel){0};
  g_netSChannelReady = false;
}

typedef struct sNetTls {
  Allocator*                alloc;
  String                    host;
  NetTlsFlags               flags;
  NetResult                 status;
  bool                      connected;
  bool                      contextCreated;
  CtxtHandle                context;
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
#ifdef SEC_E_APPLICATION_PROTOCOL_MISMATCH
  case SEC_E_APPLICATION_PROTOCOL_MISMATCH: return string_lit("APPLICATION_PROTOCOL_MISMATCH");
#endif
  case SEC_E_CERT_UNKNOWN:                  return string_lit("CERT_UNKNOWN");
  case SEC_E_CERT_EXPIRED:                  return string_lit("CERT_EXPIRED");
  case SEC_E_UNTRUSTED_ROOT:                return string_lit("UNTRUSTED_ROOT");
  default:                                  return string_lit("UNKNOWN");
  }
  // clang-format on
}

static CredHandle* net_tls_creds(NetTls* tls) {
  diag_assert(g_netSChannelReady);
  if (tls->flags & NetTlsFlags_NoVerify) {
    return &g_netSChannel.credsNoVerify;
  }
  return &g_netSChannel.creds;
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

    const SECURITY_STATUS initStatus = g_netSChannel.InitializeSecurityContextW(
        net_tls_creds(tls),
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
      dynstring_clear(&tls->readBuffer); // SChannel consumed all the data.
    }

    // Send data to remote.
    if (buffersOut[0].pvBuffer) {
      tls->status = net_tls_write_buffer_sync(buffersOut[0], socket);
      g_netSChannel.FreeContextBuffer(buffersOut[0].pvBuffer);
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
  g_netSChannel.QueryContextAttributesW(&tls->context, SECPKG_ATTR_STREAM_SIZES, &tls->sizes);
}

NetTls* net_tls_create(Allocator* alloc, const String host, const NetTlsFlags flags) {
  NetTls* tls = alloc_alloc_t(alloc, NetTls);

  *tls = (NetTls){
      .alloc      = alloc,
      .host       = string_maybe_dup(alloc, host),
      .flags      = flags,
      .readBuffer = dynstring_create(g_allocHeap, usize_kibibyte * 16),
  };
  if (UNLIKELY(!g_netSChannelReady)) {
    tls->status = NetResult_TlsUnavailable;
    return tls;
  }
  // NOTE: Connect will be performed on the first read / write.
  return tls;
}

void net_tls_destroy(NetTls* tls) {
  if (tls->contextCreated) {
    g_netSChannel.DeleteSecurityContext(&tls->context);
  }
  string_maybe_free(tls->alloc, tls->host);
  dynstring_destroy(&tls->readBuffer);
  alloc_free_t(tls->alloc, tls);
}

NetResult net_tls_status(const NetTls* tls) { return tls->status; }

NetResult net_tls_write_sync(NetTls* tls, NetSocket* socket, String data) {
  if (tls->status != NetResult_Success) {
    return tls->status;
  }
  diag_assert(g_netSChannelReady);

  if (!tls->connected) {
    net_tls_connect_sync(tls, socket);
    if (tls->status != NetResult_Success) {
      return tls->status;
    }
    tls->connected = true;
  }

  while (!string_is_empty(data)) {
    const usize  messageSize = math_min(data.size, (usize)tls->sizes.cbMaximumMessage);
    const String message     = string_slice(data, 0, messageSize);

    const usize writeBufferSize = messageSize + tls->sizes.cbHeader + tls->sizes.cbTrailer;
    const Mem   writeBuffer     = alloc_alloc(g_allocScratch, writeBufferSize, 1);

    mem_cpy(mem_consume(writeBuffer, tls->sizes.cbHeader), message);

    // clang-format off
    SecBuffer buffers[] = {
      [0] = { .BufferType = SECBUFFER_STREAM_HEADER,
              .pvBuffer   = writeBuffer.ptr,
              .cbBuffer   = tls->sizes.cbHeader },
      [1] = {
              .BufferType = SECBUFFER_DATA,
              .pvBuffer   = bits_ptr_offset(writeBuffer.ptr, tls->sizes.cbHeader),
              .cbBuffer   = (unsigned long)messageSize },
      [2] = {
              .BufferType = SECBUFFER_STREAM_TRAILER,
              .pvBuffer   = bits_ptr_offset(writeBuffer.ptr, tls->sizes.cbHeader + messageSize),
              .cbBuffer   = tls->sizes.cbTrailer },
    };
    SecBufferDesc bufferDesc = {SECBUFFER_VERSION, array_elems(buffers), buffers};
    // clang-format on

    const SECURITY_STATUS encryptStatus =
        g_netSChannel.EncryptMessage(&tls->context, 0, &bufferDesc, 0);

    if (encryptStatus != SEC_E_OK) {
      log_e(
          "SChannel encrypt failed",
          log_param("msg", fmt_text(net_tls_schannel_error_msg(encryptStatus))),
          log_param("code", fmt_int((u32)encryptStatus)));
      return tls->status = NetResult_TlsFailed;
    }

    const usize writeSize = buffers[0].cbBuffer + buffers[1].cbBuffer + buffers[2].cbBuffer;
    tls->status           = net_socket_write_sync(socket, mem_slice(writeBuffer, 0, writeSize));
    if (tls->status != NetResult_Success) {
      return tls->status;
    }
    data = string_consume(data, messageSize);
  }
  return NetResult_Success;
}

NetResult net_tls_read_sync(NetTls* tls, NetSocket* socket, DynString* out) {
  if (tls->status != NetResult_Success) {
    return tls->status;
  }
  diag_assert(g_netSChannelReady);

  if (!tls->connected) {
    net_tls_connect_sync(tls, socket);
    if (tls->status != NetResult_Success) {
      return tls->status;
    }
    tls->connected = true;
  }

  usize totalBytesRead = 0;
  for (;;) {
    const String        dataIn     = dynstring_view(&tls->readBuffer);
    const unsigned long dataInSize = (unsigned long)dataIn.size;
    SecBuffer           buffers[]  = {
        [0] = {.BufferType = SECBUFFER_DATA, .pvBuffer = dataIn.ptr, .cbBuffer = dataInSize},
        [1] = {.BufferType = SECBUFFER_EMPTY},
        [2] = {.BufferType = SECBUFFER_EMPTY},
        [3] = {.BufferType = SECBUFFER_EMPTY},
    };
    diag_assert(array_elems(buffers) == tls->sizes.cBuffers);

    SecBufferDesc bufferDesc = {SECBUFFER_VERSION, array_elems(buffers), buffers};

    SECURITY_STATUS decryptStatus =
        g_netSChannel.DecryptMessage(&tls->context, &bufferDesc, 0, null);

    switch (decryptStatus) {
    case SEC_E_OK:
      diag_assert(buffers[0].BufferType == SECBUFFER_STREAM_HEADER);
      diag_assert(buffers[1].BufferType == SECBUFFER_DATA);
      diag_assert(buffers[2].BufferType == SECBUFFER_STREAM_TRAILER);

      // Write decrypted output.
      dynstring_append(out, mem_create(buffers[1].pvBuffer, buffers[1].cbBuffer));
      totalBytesRead += buffers[1].cbBuffer;

      // Consume data from the input buffer.
      if (buffers[3].BufferType == SECBUFFER_EXTRA) {
        diag_assert(tls->readBuffer.size >= buffers[3].cbBuffer);
        dynstring_erase_chars(&tls->readBuffer, 0, tls->readBuffer.size - buffers[3].cbBuffer);
        diag_assert(tls->readBuffer.size == buffers[3].cbBuffer);
      } else if (buffers[3].BufferType != SECBUFFER_MISSING) {
        dynstring_clear(&tls->readBuffer); // SChannel consumed all the data.
      }
      break;
    case SEC_I_CONTEXT_EXPIRED:
      tls->status = NetResult_TlsClosed;
      return totalBytesRead ? NetResult_Success : NetResult_TlsClosed;
    case SEC_I_RENEGOTIATE:
      log_e("SChannel renegotiation required");
      return tls->status = NetResult_TlsFailed;
    case SEC_E_INCOMPLETE_MESSAGE:
      if (totalBytesRead) {
        return NetResult_Success; // We've successfully read all the available data.
      }
      tls->status = net_socket_read_sync(socket, &tls->readBuffer);
      if (tls->status != NetResult_Success) {
        return tls->status;
      }
      continue; // More data available; retry.
    default:
      log_e(
          "SChannel decrypt failed",
          log_param("msg", fmt_text(net_tls_schannel_error_msg(decryptStatus))),
          log_param("code", fmt_int((u32)decryptStatus)));
      return tls->status = NetResult_TlsFailed;
    }
  }
}

NetResult net_tls_shutdown_sync(NetTls* tls, NetSocket* socket) {
  if (!tls->connected || tls->status == NetResult_TlsClosed) {
    return NetResult_Success; // Session already closed, no need to shutdown.
  }
  diag_assert(g_netSChannelReady);

  DWORD type = SCHANNEL_SHUTDOWN;

  SecBuffer buffersIn[] = {
      [0] = {.BufferType = SECBUFFER_TOKEN, .pvBuffer = &type, .cbBuffer = sizeof(type)},
  };
  SecBufferDesc descIn = {SECBUFFER_VERSION, array_elems(buffersIn), buffersIn};
  g_netSChannel.ApplyControlToken(&tls->context, &descIn);

  SecBuffer buffersOut[] = {
      [0] = {.BufferType = SECBUFFER_TOKEN},
  };
  SecBufferDesc descOut = {SECBUFFER_VERSION, array_elems(buffersOut), buffersOut};

  DWORD flags = ISC_REQ_ALLOCATE_MEMORY | ISC_REQ_CONFIDENTIALITY | ISC_REQ_REPLAY_DETECT |
                ISC_REQ_SEQUENCE_DETECT | ISC_REQ_STREAM;

  const SECURITY_STATUS shutdownStatus = g_netSChannel.InitializeSecurityContextW(
      net_tls_creds(tls),
      &tls->context,
      null,
      flags,
      0,
      0,
      &descOut,
      0,
      null,
      &descOut,
      &flags,
      null);

  // Send data to remote.
  if (buffersOut[0].pvBuffer) {
    tls->status = net_tls_write_buffer_sync(buffersOut[0], socket);
    g_netSChannel.FreeContextBuffer(buffersOut[0].pvBuffer);
    if (tls->status != NetResult_Success) {
      return tls->status; // Shutdown failed.
    }
  }

  if (shutdownStatus == SEC_E_OK) {
    dynstring_clear(&tls->readBuffer); // Discard any remaining input.
    tls->status = NetResult_TlsClosed;
    return NetResult_Success; // Shutdown successful.
  }

  log_e(
      "SChannel shutdown failed",
      log_param("msg", fmt_text(net_tls_schannel_error_msg(NetResult_TlsClosed))),
      log_param("code", fmt_int((u32)NetResult_TlsClosed)));
  return tls->status = NetResult_TlsFailed;
}
