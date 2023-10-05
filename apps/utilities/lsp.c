#include "app_cli.h"
#include "core_alloc.h"
#include "core_file.h"
#include "core_format.h"
#include "core_path.h"
#include "json.h"

/**
 * Language Server Protocol implementation for the Volo script language.
 *
 * Specification:
 * https://microsoft.github.io/language-server-protocol/specifications/lsp/3.17/specification/
 */

typedef enum {
  LspStatus_Running,
  LspStatus_Shutdown,
  LspStatus_ErrorReadFailed,
  LspStatus_ErrorInvalidJson,
  LspStatus_ErrorInvalidJRpcMessage,
  LspStatus_ErrorUnsupportedJRpcVersion,
  LspStatus_ErrorMalformedRequest,

  LspStatus_Count,
} LspStatus;

static const String g_lspStatusMessage[LspStatus_Count] = {
    [LspStatus_Running]                     = string_static("Running"),
    [LspStatus_Shutdown]                    = string_static("Shutdown"),
    [LspStatus_ErrorReadFailed]             = string_static("Error: Read failed"),
    [LspStatus_ErrorInvalidJson]            = string_static("Error: Invalid json received"),
    [LspStatus_ErrorInvalidJRpcMessage]     = string_static("Error: Invalid jrpc message received"),
    [LspStatus_ErrorUnsupportedJRpcVersion] = string_static("Error: Unsupported jrpc version"),
    [LspStatus_ErrorMalformedRequest]       = string_static("Error: Malformed request"),
};

typedef enum {
  LspFlags_Initialized = 1 << 0,
} LspFlags;

typedef struct {
  LspStatus  status;
  LspFlags   flags;
  DynString* readBuffer;
  usize      readCursor;
  DynString* writeBuffer;
  JsonDoc*   jsonDoc; // Cleared between requests.
  File*      in;
  File*      out;
} LspContext;

typedef struct {
  usize contentLength;
} LspHeader;

typedef struct {
  String  method;
  JsonVal params; // Optional, sentinel_u32 if unused.
} JRpcNotification;

typedef struct {
  String  method;
  JsonVal params; // Optional, sentinel_u32 if unused.
  JsonVal id;
} JRpcRequest;

typedef struct {
  i32    code;
  String msg;
} JRpcError;

// clang-format off

static const JRpcError g_jrpcErrorNotInitialized = {.code = -32002, .msg = string_static("Server not initialized")};
static const JRpcError g_jrpcErrorMethodNotFound = {.code = -32601, .msg = string_static("Method not found")};

// clang-format on

static void lsp_output_err(const String msg) {
  const String appName = path_filename(g_path_executable);
  const String text    = fmt_write_scratch("{}: {}\n", fmt_text(appName), fmt_text(msg));
  file_write_sync(g_file_stderr, text);
}

static void lsp_read_trim(LspContext* ctx) {
  dynstring_erase_chars(ctx->readBuffer, 0, ctx->readCursor);
  ctx->readCursor = 0;
}

static String lsp_read_remaining(LspContext* ctx) {
  const String full = dynstring_view(ctx->readBuffer);
  return string_slice(full, ctx->readCursor, full.size - ctx->readCursor);
}

static void lsp_read_chunk(LspContext* ctx) {
  const FileResult res = file_read_sync(ctx->in, ctx->readBuffer);
  if (UNLIKELY(res != FileResult_Success)) {
    ctx->status = LspStatus_ErrorReadFailed;
  }
}

static String lsp_read_until(LspContext* ctx, const String pattern) {
  while (LIKELY(ctx->status == LspStatus_Running)) {
    const String text = lsp_read_remaining(ctx);
    const usize  pos  = string_find_first(text, pattern);
    if (!sentinel_check(pos)) {
      ctx->readCursor += pos + pattern.size;
      return string_slice(text, 0, pos + pattern.size);
    }
    lsp_read_chunk(ctx);
  }
  return string_empty;
}

static String lsp_read_sized(LspContext* ctx, const usize size) {
  while (LIKELY(ctx->status == LspStatus_Running)) {
    const String text = lsp_read_remaining(ctx);
    if (text.size >= size) {
      ctx->readCursor += size;
      return string_slice(text, 0, size);
    }
    lsp_read_chunk(ctx);
  }
  return string_empty;
}

static String lsp_header_lex_key(const String input, String* outKey) {
  const usize colonPos = string_find_first(input, string_lit(": "));
  if (sentinel_check(colonPos)) {
    *outKey = string_empty;
    return input;
  }
  *outKey = string_trim_whitespace(string_slice(input, 0, colonPos));
  return string_consume(input, colonPos + 2);
}

static LspHeader lsp_read_header(LspContext* ctx) {
  LspHeader result = {0};
  String    input  = lsp_read_until(ctx, string_lit("\r\n\r\n"));
  while (LIKELY(ctx->status == LspStatus_Running)) {
    String key;
    input = lsp_header_lex_key(input, &key);
    if (string_is_empty(key)) {
      break;
    }
    if (string_eq(key, string_lit("Content-Length"))) {
      input = format_read_u64(input, &result.contentLength, 10);
    }
    // Consume the rest of the line.
    const usize lineEndPos = string_find_first_char(input, '\n');
    input                  = string_consume(input, sentinel_check(lineEndPos) ? 0 : lineEndPos + 1);
  }
  return result;
}

static void lsp_send_json(LspContext* ctx, const JsonVal val) {
  const JsonWriteOpts writeOpts = json_write_opts(.flags = JsonWriteFlags_None);
  json_write(ctx->writeBuffer, ctx->jsonDoc, val, &writeOpts);

  const usize  contentSize = ctx->writeBuffer->size;
  const String headerText  = fmt_write_scratch("Content-Length: {}\r\n\r\n", fmt_int(contentSize));
  dynstring_insert(ctx->writeBuffer, headerText, 0);

  file_write_sync(ctx->out, dynstring_view(ctx->writeBuffer));
  dynstring_clear(ctx->writeBuffer);
}

static void lsp_send_response_success(LspContext* ctx, const JRpcRequest* req, const JsonVal val) {
  const JsonVal response = json_add_object(ctx->jsonDoc);
  json_add_field_lit(ctx->jsonDoc, response, "jsonrpc", json_add_string_lit(ctx->jsonDoc, "2.0"));
  json_add_field_lit(ctx->jsonDoc, response, "result", val);

  JsonVal responseId;
  switch (json_type(ctx->jsonDoc, req->id)) {
  case JsonType_Number:
    responseId = json_add_number(ctx->jsonDoc, json_number(ctx->jsonDoc, req->id));
    break;
  case JsonType_String:
    responseId = json_add_string(ctx->jsonDoc, json_string(ctx->jsonDoc, req->id));
    break;
  default:
    responseId = json_add_null(ctx->jsonDoc);
  }
  json_add_field_lit(ctx->jsonDoc, response, "id", responseId);

  lsp_send_json(ctx, response);
}

static void lsp_send_response_error(LspContext* ctx, const JRpcRequest* req, const JRpcError* err) {
  const JsonVal errObj = json_add_object(ctx->jsonDoc);
  json_add_field_lit(ctx->jsonDoc, errObj, "code", json_add_number(ctx->jsonDoc, err->code));
  json_add_field_lit(ctx->jsonDoc, errObj, "message", json_add_string(ctx->jsonDoc, err->msg));

  const JsonVal response = json_add_object(ctx->jsonDoc);
  json_add_field_lit(ctx->jsonDoc, response, "jsonrpc", json_add_string_lit(ctx->jsonDoc, "2.0"));
  json_add_field_lit(ctx->jsonDoc, response, "error", errObj);

  JsonVal responseId;
  switch (json_type(ctx->jsonDoc, req->id)) {
  case JsonType_Number:
    responseId = json_add_number(ctx->jsonDoc, json_number(ctx->jsonDoc, req->id));
    break;
  case JsonType_String:
    responseId = json_add_string(ctx->jsonDoc, json_string(ctx->jsonDoc, req->id));
    break;
  default:
    responseId = json_add_null(ctx->jsonDoc);
  }
  json_add_field_lit(ctx->jsonDoc, response, "id", responseId);

  lsp_send_json(ctx, response);
}

static void lsp_handle_notification_initialized(LspContext* ctx, const JRpcNotification* notif) {
  (void)notif;
  ctx->flags |= LspFlags_Initialized;
}

static void lsp_handle_notification(LspContext* ctx, const JRpcNotification* notif) {
  if (string_eq(notif->method, string_lit("initialized"))) {
    lsp_handle_notification_initialized(ctx, notif);
  }
}

static void lsp_handle_request_initialize(LspContext* ctx, const JRpcRequest* req) {
  if (UNLIKELY(sentinel_check(req->params))) {
    goto MalformedRequest;
  }
  if (UNLIKELY(json_type(ctx->jsonDoc, req->params) != JsonType_Object)) {
    goto MalformedRequest;
  }

  const JsonVal capabilities = json_add_object(ctx->jsonDoc);

  // NOTE: At the time of writing VSCode only supports utf-16 position encoding.
  const JsonVal positionEncoding = json_add_string_lit(ctx->jsonDoc, "utf-16");
  const JsonVal textDocumentSync = json_add_number(ctx->jsonDoc, 1); // 'Full'.
  json_add_field_lit(ctx->jsonDoc, capabilities, "positionEncoding", positionEncoding);
  json_add_field_lit(ctx->jsonDoc, capabilities, "textDocumentSync", textDocumentSync);

  const JsonVal info          = json_add_object(ctx->jsonDoc);
  const JsonVal serverName    = json_add_string_lit(ctx->jsonDoc, "Volo Language Server");
  const JsonVal serverVersion = json_add_string_lit(ctx->jsonDoc, "0.1");
  json_add_field_lit(ctx->jsonDoc, info, "name", serverName);
  json_add_field_lit(ctx->jsonDoc, info, "version", serverVersion);

  const JsonVal result = json_add_object(ctx->jsonDoc);
  json_add_field_lit(ctx->jsonDoc, result, "capabilities", capabilities);
  json_add_field_lit(ctx->jsonDoc, result, "serverInfo", info);

  lsp_send_response_success(ctx, req, result);
  return;

MalformedRequest:
  ctx->status = LspStatus_ErrorMalformedRequest;
}

static void lsp_handle_request(LspContext* ctx, const JRpcRequest* req) {
  if (string_eq(req->method, string_lit("initialize"))) {
    lsp_handle_request_initialize(ctx, req);
    return;
  }

  if (UNLIKELY(!(ctx->flags & LspFlags_Initialized))) {
    lsp_send_response_error(ctx, req, &g_jrpcErrorNotInitialized);
    return;
  }

  lsp_send_response_error(ctx, req, &g_jrpcErrorMethodNotFound);
}

static void lsp_handle_jrpc(LspContext* ctx, const JsonVal value) {
  if (UNLIKELY(json_type(ctx->jsonDoc, value) != JsonType_Object)) {
    ctx->status = LspStatus_ErrorInvalidJRpcMessage;
    return;
  }
  const JsonVal version = json_field(ctx->jsonDoc, value, string_lit("jsonrpc"));
  if (UNLIKELY(sentinel_check(version) || json_type(ctx->jsonDoc, version) != JsonType_String)) {
    ctx->status = LspStatus_ErrorInvalidJRpcMessage;
    return;
  }
  if (UNLIKELY(!string_eq(json_string(ctx->jsonDoc, version), string_lit("2.0")))) {
    ctx->status = LspStatus_ErrorUnsupportedJRpcVersion;
    return;
  }
  const JsonVal method = json_field(ctx->jsonDoc, value, string_lit("method"));
  if (UNLIKELY(sentinel_check(method) || json_type(ctx->jsonDoc, method) != JsonType_String)) {
    ctx->status = LspStatus_ErrorInvalidJRpcMessage;
    return;
  }
  const JsonVal params = json_field(ctx->jsonDoc, value, string_lit("params"));
  const JsonVal id     = json_field(ctx->jsonDoc, value, string_lit("id"));

  lsp_output_err(json_string(ctx->jsonDoc, method));

  if (sentinel_check(id)) {
    const JRpcNotification notification = {
        .method = json_string(ctx->jsonDoc, method),
        .params = params,
    };
    lsp_handle_notification(ctx, &notification);
  } else {
    const JRpcRequest request = {
        .method = json_string(ctx->jsonDoc, method),
        .params = params,
        .id     = id,
    };
    lsp_handle_request(ctx, &request);
  }
}

static i32 lsp_run_stdio() {
  DynString readBuffer  = dynstring_create(g_alloc_heap, 8 * usize_kibibyte);
  DynString writeBuffer = dynstring_create(g_alloc_heap, 2 * usize_kibibyte);
  JsonDoc*  jsonDoc     = json_create(g_alloc_heap, 1024);

  LspContext ctx = {
      .status      = LspStatus_Running,
      .readBuffer  = &readBuffer,
      .writeBuffer = &writeBuffer,
      .jsonDoc     = jsonDoc,
      .in          = g_file_stdin,
      .out         = g_file_stdout,
  };

  while (LIKELY(ctx.status == LspStatus_Running)) {
    const LspHeader header  = lsp_read_header(&ctx);
    const String    content = lsp_read_sized(&ctx, header.contentLength);

    JsonResult jsonResult;
    json_read(jsonDoc, content, &jsonResult);
    if (UNLIKELY(jsonResult.type == JsonResultType_Fail)) {
      const String jsonErrMsg = json_error_str(jsonResult.error);
      lsp_output_err(fmt_write_scratch("Json read failed: {}", fmt_text(jsonErrMsg)));
      ctx.status = LspStatus_ErrorInvalidJson;
      break;
    }

    lsp_handle_jrpc(&ctx, jsonResult.val);

    lsp_read_trim(&ctx);
    json_clear(jsonDoc);
  }

  json_destroy(jsonDoc);
  dynstring_destroy(&readBuffer);
  dynstring_destroy(&writeBuffer);

  if (ctx.status != LspStatus_Shutdown) {
    lsp_output_err(g_lspStatusMessage[ctx.status]);
    return 1;
  }
  return 0;
}

static CliId g_optHelp, g_stdioFlag;

void app_cli_configure(CliApp* app) {
  cli_app_register_desc(app, string_lit("Volo Script Language Server"));

  g_stdioFlag = cli_register_flag(app, 0, string_lit("stdio"), CliOptionFlags_None);
  cli_register_desc(app, g_stdioFlag, string_lit("Use stdin and stdout for communication."));

  g_optHelp = cli_register_flag(app, 'h', string_lit("help"), CliOptionFlags_None);
  cli_register_desc(app, g_optHelp, string_lit("Display this help page."));
  cli_register_exclusions(app, g_optHelp, g_stdioFlag);
}

i32 app_cli_run(const CliApp* app, const CliInvocation* invoc) {
  if (cli_parse_provided(invoc, g_optHelp)) {
    cli_help_write_file(app, g_file_stdout);
    return 0;
  }

  if (cli_parse_provided(invoc, g_stdioFlag)) {
    return lsp_run_stdio();
  }

  lsp_output_err(string_lit("No communication method specified."));
  return 1;
}
