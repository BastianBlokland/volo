#pragma once
#include "net.h"

typedef enum eNetResult {
  NetResult_Success = 0,
  NetResult_SystemFailure,
  NetResult_Unsupported,
  NetResult_Refused,
  NetResult_Unreachable,
  NetResult_ConnectionClosed,
  NetResult_ConnectionLost,
  NetResult_NoEntry,
  NetResult_InvalidHost,
  NetResult_HostNotFound,
  NetResult_TryAgain,
  NetResult_TooMuchData,
  NetResult_TlsUnavailable,
  NetResult_TlsFailed,
  NetResult_TlsClosed,
  NetResult_TlsBufferExhausted,
  NetResult_HttpNotModified,
  NetResult_HttpNotFound,
  NetResult_HttpUnauthorized,
  NetResult_HttpForbidden,
  NetResult_HttpRedirected,
  NetResult_HttpServerError,
  NetResult_HttpClientError,
  NetResult_HttpUnsupportedProtocol,
  NetResult_HttpUnsupportedVersion,
  NetResult_HttpUnsupportedTransferEncoding,
  NetResult_HttpUnsupportedContentEncoding,
  NetResult_HttpMalformedHeader,
  NetResult_HttpMalformedChunk,
  NetResult_HttpMalformedCompression,
  NetResult_HttpUnexpectedData,
  NetResult_UnknownError,

  NetResult_Count,
} NetResult;

/**
 * Return a textual representation of the given NetResult.
 */
String net_result_str(NetResult);
