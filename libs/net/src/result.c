#include "core_array.h"
#include "core_diag.h"
#include "core_string.h"
#include "net_result.h"

static const String g_netResultStrs[] = {
    [NetResult_Success]            = string_static("Success"),
    [NetResult_SystemFailure]      = string_static("SystemFailure"),
    [NetResult_Unsupported]        = string_static("Unsupported"),
    [NetResult_Refused]            = string_static("Refused"),
    [NetResult_Unreachable]        = string_static("Unreachable"),
    [NetResult_ConnectionClosed]   = string_static("ConnectionClosed"),
    [NetResult_ConnectionLost]     = string_static("ConnectionLost"),
    [NetResult_NoEntry]            = string_static("NoEntry"),
    [NetResult_InvalidHost]        = string_static("InvalidHost"),
    [NetResult_HostNotFound]       = string_static("HostNotFound"),
    [NetResult_TryAgain]           = string_static("TryAgain"),
    [NetResult_TooMuchData]        = string_static("TooMuchData"),
    [NetResult_TlsUnavailable]     = string_static("TlsUnavailable"),
    [NetResult_TlsFailed]          = string_static("TlsFailed"),
    [NetResult_TlsBufferExhausted] = string_static("TlsBufferExhausted"),
    [NetResult_UnknownError]       = string_static("UnknownError"),
};

ASSERT(array_elems(g_netResultStrs) == NetResult_Count, "Incorrect number of result strings");

String net_result_str(const NetResult result) {
  diag_assert(result < NetResult_Count);
  return g_netResultStrs[result];
}
