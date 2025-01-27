#include "core_array.h"
#include "core_diag.h"
#include "core_string.h"
#include "net_result.h"

static const String g_netResultStrs[] = {
    [NetResult_Success]                         = string_static("NetSuccess"),
    [NetResult_SystemFailure]                   = string_static("NetSystemFailure"),
    [NetResult_Unsupported]                     = string_static("NetUnsupported"),
    [NetResult_Refused]                         = string_static("NetRefused"),
    [NetResult_Unreachable]                     = string_static("NetUnreachable"),
    [NetResult_ConnectionClosed]                = string_static("NetConnectionClosed"),
    [NetResult_ConnectionLost]                  = string_static("NetConnectionLost"),
    [NetResult_NoEntry]                         = string_static("NetNoEntry"),
    [NetResult_InvalidHost]                     = string_static("NetInvalidHost"),
    [NetResult_HostNotFound]                    = string_static("NetHostNotFound"),
    [NetResult_TryAgain]                        = string_static("NetTryAgain"),
    [NetResult_TooMuchData]                     = string_static("NetTooMuchData"),
    [NetResult_TlsUnavailable]                  = string_static("NetTlsUnavailable"),
    [NetResult_TlsFailed]                       = string_static("NetTlsFailed"),
    [NetResult_TlsClosed]                       = string_static("NetTlsClosed"),
    [NetResult_TlsBufferExhausted]              = string_static("NetTlsBufferExhausted"),
    [NetResult_HttpNotFound]                    = string_static("HttpNotFound"),
    [NetResult_HttpUnauthorized]                = string_static("HttpUnauthorized"),
    [NetResult_HttpForbidden]                   = string_static("HttpForbidden"),
    [NetResult_HttpRedirected]                  = string_static("HttpRedirected"),
    [NetResult_HttpServerError]                 = string_static("HttpServerError"),
    [NetResult_HttpClientError]                 = string_static("HttpClientError"),
    [NetResult_HttpUnsupportedProtocol]         = string_static("HttpUnsupportedProtocol"),
    [NetResult_HttpUnsupportedVersion]          = string_static("HttpUnsupportedVersion"),
    [NetResult_HttpUnsupportedTransferEncoding] = string_static("HttpUnsupportedTransferEncoding"),
    [NetResult_HttpUnsupportedContentEncoding]  = string_static("HttpUnsupportedContentEncoding"),
    [NetResult_HttpMalformedHeader]             = string_static("HttpMalformedHeader"),
    [NetResult_HttpMalformedChunk]              = string_static("HttpMalformedChunk"),
    [NetResult_HttpUnexpectedData]              = string_static("HttpUnexpectedData"),
    [NetResult_UnknownError]                    = string_static("NetUnknownError"),
};

ASSERT(array_elems(g_netResultStrs) == NetResult_Count, "Incorrect number of result strings");

String net_result_str(const NetResult result) {
  diag_assert(result < NetResult_Count);
  return g_netResultStrs[result];
}
