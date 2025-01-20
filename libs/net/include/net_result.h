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
  NetResult_UnknownError,

  NetResult_Count,
} NetResult;

/**
 * Return a textual representation of the given NetResult.
 */
String net_result_str(NetResult);
