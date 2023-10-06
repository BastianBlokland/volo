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
  LspStatus_Exit,
  LspStatus_ErrorReadFailed,
  LspStatus_ErrorInvalidJson,
  LspStatus_ErrorInvalidJRpcMessage,
  LspStatus_ErrorUnsupportedJRpcVersion,
  LspStatus_ErrorMalformedRequest,

  LspStatus_Count,
} LspStatus;

static const String g_lspStatusMessage[LspStatus_Count] = {
    [LspStatus_Running]                     = string_static("Running"),
    [LspStatus_Exit]                        = string_static("Exit"),
    [LspStatus_ErrorReadFailed]             = string_static("Error: Read failed"),
    [LspStatus_ErrorInvalidJson]            = string_static("Error: Invalid json received"),
    [LspStatus_ErrorInvalidJRpcMessage]     = string_static("Error: Invalid jrpc message received"),
    [LspStatus_ErrorUnsupportedJRpcVersion] = string_static("Error: Unsupported jrpc version"),
    [LspStatus_ErrorMalformedRequest]       = string_static("Error: Malformed request"),
};

typedef enum {
  LspFlags_Initialized = 1 << 0,
  LspFlags_Shutdown    = 1 << 1,
  LspFlags_Trace       = 1 << 2,
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

typedef enum {
  LspMessageType_Error   = 1,
  LspMessageType_Warning = 2,
  LspMessageType_Info    = 3,
  LspMessageType_Log     = 4,
} LspMessageType;

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

static const JRpcError g_jrpcErrorMethodNotFound = {
    .code = -32601,
    .msg  = string_static("Method not found"),
};

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

static String lsp_maybe_str(LspContext* ctx, const JsonVal val) {
  if (sentinel_check(val) || json_type(ctx->jsonDoc, val) != JsonType_String) {
    return string_empty;
  }
  return json_string(ctx->jsonDoc, val);
}

static void lsp_update_trace(LspContext* ctx, const JsonVal traceValue) {
  if (string_eq(lsp_maybe_str(ctx, traceValue), string_lit("off"))) {
    ctx->flags &= ~LspFlags_Trace;
  } else {
    ctx->flags |= LspFlags_Trace;
  }
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

static void lsp_send_notification(LspContext* ctx, const JRpcNotification* notif) {
  const JsonVal resp = json_add_object(ctx->jsonDoc);
  json_add_field_lit(ctx->jsonDoc, resp, "jsonrpc", json_add_string_lit(ctx->jsonDoc, "2.0"));
  json_add_field_lit(ctx->jsonDoc, resp, "method", json_add_string(ctx->jsonDoc, notif->method));
  if (!sentinel_check(notif->params)) {
    json_add_field_lit(ctx->jsonDoc, resp, "params", notif->params);
  }

  lsp_send_json(ctx, resp);
}

static void lsp_send_trace(LspContext* ctx, const String message) {
  const JsonVal params = json_add_object(ctx->jsonDoc);
  json_add_field_lit(ctx->jsonDoc, params, "message", json_add_string(ctx->jsonDoc, message));

  const JRpcNotification notif = {
      .method = string_lit("$/logTrace"),
      .params = params,
  };
  lsp_send_notification(ctx, &notif);
}

static void lsp_send_log(LspContext* ctx, const LspMessageType type, const String message) {
  const JsonVal params = json_add_object(ctx->jsonDoc);
  json_add_field_lit(ctx->jsonDoc, params, "type", json_add_number(ctx->jsonDoc, type));
  json_add_field_lit(ctx->jsonDoc, params, "message", json_add_string(ctx->jsonDoc, message));

  const JRpcNotification notif = {
      .method = string_lit("window/logMessage"),
      .params = params,
  };
  lsp_send_notification(ctx, &notif);
}

static void lsp_send_response_success(LspContext* ctx, const JRpcRequest* req, const JsonVal val) {
  const JsonVal resp = json_add_object(ctx->jsonDoc);
  json_add_field_lit(ctx->jsonDoc, resp, "jsonrpc", json_add_string_lit(ctx->jsonDoc, "2.0"));
  json_add_field_lit(ctx->jsonDoc, resp, "result", val);

  JsonVal respId;
  switch (json_type(ctx->jsonDoc, req->id)) {
  case JsonType_Number:
    respId = json_add_number(ctx->jsonDoc, json_number(ctx->jsonDoc, req->id));
    break;
  case JsonType_String:
    respId = json_add_string(ctx->jsonDoc, json_string(ctx->jsonDoc, req->id));
    break;
  default:
    respId = json_add_null(ctx->jsonDoc);
  }
  json_add_field_lit(ctx->jsonDoc, resp, "id", respId);

  lsp_send_json(ctx, resp);
}

static void lsp_send_response_error(LspContext* ctx, const JRpcRequest* req, const JRpcError* err) {
  const JsonVal errObj = json_add_object(ctx->jsonDoc);
  json_add_field_lit(ctx->jsonDoc, errObj, "code", json_add_number(ctx->jsonDoc, err->code));
  json_add_field_lit(ctx->jsonDoc, errObj, "message", json_add_string(ctx->jsonDoc, err->msg));

  const JsonVal resp = json_add_object(ctx->jsonDoc);
  json_add_field_lit(ctx->jsonDoc, resp, "jsonrpc", json_add_string_lit(ctx->jsonDoc, "2.0"));
  json_add_field_lit(ctx->jsonDoc, resp, "error", errObj);

  JsonVal respId;
  switch (json_type(ctx->jsonDoc, req->id)) {
  case JsonType_Number:
    respId = json_add_number(ctx->jsonDoc, json_number(ctx->jsonDoc, req->id));
    break;
  case JsonType_String:
    respId = json_add_string(ctx->jsonDoc, json_string(ctx->jsonDoc, req->id));
    break;
  default:
    respId = json_add_null(ctx->jsonDoc);
  }
  json_add_field_lit(ctx->jsonDoc, resp, "id", respId);

  lsp_send_json(ctx, resp);
}

static void lsp_handle_notif_initialized(LspContext* ctx, const JRpcNotification* notif) {
  (void)notif;
  ctx->flags |= LspFlags_Initialized;

  lsp_send_log(ctx, LspMessageType_Info, string_lit("Server successfully initialized"));
}

static void lsp_handle_notif_exit(LspContext* ctx, const JRpcNotification* notif) {
  (void)notif;
  ctx->status = LspStatus_Exit;
}

static void lsp_handle_notif_set_trace(LspContext* ctx, const JRpcNotification* notif) {
  if (sentinel_check(notif->params) || json_type(ctx->jsonDoc, notif->params) != JsonType_Object) {
    return; // TODO: Report error.
  }
  const JsonVal traceVal = json_field(ctx->jsonDoc, notif->params, string_lit("value"));
  lsp_update_trace(ctx, traceVal);
}

static void lsp_handle_notif_doc_did_open(LspContext* ctx, const JRpcNotification* notif) {
  if (sentinel_check(notif->params) || json_type(ctx->jsonDoc, notif->params) != JsonType_Object) {
    return; // TODO: Report error.
  }
  const JsonVal docVal = json_field(ctx->jsonDoc, notif->params, string_lit("textDocument"));
  if (sentinel_check(docVal) || json_type(ctx->jsonDoc, docVal) != JsonType_Object) {
    return; // TODO: Report error.
  }
  const String uri = lsp_maybe_str(ctx, json_field(ctx->jsonDoc, docVal, string_lit("uri")));
  if (string_is_empty(uri)) {
    return; // TODO: Report error.
  }
  const String text = lsp_maybe_str(ctx, json_field(ctx->jsonDoc, docVal, string_lit("text")));
  if (string_is_empty(text)) {
    return; // TODO: Report error.
  }

  lsp_send_trace(ctx, fmt_write_scratch("Open: {}", fmt_text(uri)));
  // TODO: Process script text.
}

static void lsp_handle_notif_doc_did_change(LspContext* ctx, const JRpcNotification* notif) {
  if (sentinel_check(notif->params) || json_type(ctx->jsonDoc, notif->params) != JsonType_Object) {
    return; // TODO: Report error.
  }
  const JsonVal docVal = json_field(ctx->jsonDoc, notif->params, string_lit("textDocument"));
  if (sentinel_check(docVal) || json_type(ctx->jsonDoc, docVal) != JsonType_Object) {
    return; // TODO: Report error.
  }
  const String uri = lsp_maybe_str(ctx, json_field(ctx->jsonDoc, docVal, string_lit("uri")));
  if (string_is_empty(uri)) {
    return; // TODO: Report error.
  }
  const JsonVal changesVal = json_field(ctx->jsonDoc, notif->params, string_lit("contentChanges"));
  if (sentinel_check(changesVal) || json_type(ctx->jsonDoc, changesVal) != JsonType_Array) {
    return; // TODO: Report error.
  }
  const JsonVal changeVal = json_elem_begin(ctx->jsonDoc, changesVal);
  if (sentinel_check(changeVal) || json_type(ctx->jsonDoc, changeVal) != JsonType_Object) {
    return; // TODO: Report error.
  }
  const String text = lsp_maybe_str(ctx, json_field(ctx->jsonDoc, changeVal, string_lit("text")));
  if (string_is_empty(text)) {
    return; // TODO: Report error.
  }

  lsp_send_trace(ctx, fmt_write_scratch("Change: {}", fmt_text(uri)));
  // TODO: Process script text.
}

static void lsp_handle_notif(LspContext* ctx, const JRpcNotification* notif) {
  static const struct {
    String method;
    void (*handler)(LspContext*, const JRpcNotification*);
  } g_handlers[] = {
      {string_static("initialized"), lsp_handle_notif_initialized},
      {string_static("exit"), lsp_handle_notif_exit},
      {string_static("$/setTrace"), lsp_handle_notif_set_trace},
      {string_static("textDocument/didOpen"), lsp_handle_notif_doc_did_open},
      {string_static("textDocument/didChange"), lsp_handle_notif_doc_did_change},
  };

  for (u32 i = 0; i != array_elems(g_handlers); ++i) {
    if (string_eq(notif->method, g_handlers[i].method)) {
      g_handlers[i].handler(ctx, notif);
      return;
    }
  }

  if (ctx->flags & LspFlags_Trace) {
    lsp_send_trace(ctx, fmt_write_scratch("Unhandled notification: {}", fmt_text(notif->method)));
  }
}

static void lsp_handle_req_initialize(LspContext* ctx, const JRpcRequest* req) {
  if (UNLIKELY(sentinel_check(req->params))) {
    goto MalformedRequest;
  }
  if (UNLIKELY(json_type(ctx->jsonDoc, req->params) != JsonType_Object)) {
    goto MalformedRequest;
  }

  const JsonVal traceVal = json_field(ctx->jsonDoc, req->params, string_lit("trace"));
  if (!sentinel_check(traceVal)) {
    lsp_update_trace(ctx, traceVal);
  }

  const JsonVal docSyncOptions = json_add_object(ctx->jsonDoc);
  json_add_field_lit(ctx->jsonDoc, docSyncOptions, "openClose", json_add_bool(ctx->jsonDoc, true));
  json_add_field_lit(ctx->jsonDoc, docSyncOptions, "change", json_add_number(ctx->jsonDoc, 1));

  const JsonVal capabilities = json_add_object(ctx->jsonDoc);
  // NOTE: At the time of writing VSCode only supports utf-16 position encoding.
  const JsonVal positionEncoding = json_add_string_lit(ctx->jsonDoc, "utf-16");
  json_add_field_lit(ctx->jsonDoc, capabilities, "positionEncoding", positionEncoding);
  json_add_field_lit(ctx->jsonDoc, capabilities, "textDocumentSync", docSyncOptions);

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

static void lsp_handle_req_shutdown(LspContext* ctx, const JRpcRequest* req) {
  ctx->flags |= LspFlags_Shutdown;
  lsp_send_response_success(ctx, req, json_add_null(ctx->jsonDoc));
}

static void lsp_handle_req(LspContext* ctx, const JRpcRequest* req) {
  static const struct {
    String method;
    void (*handler)(LspContext*, const JRpcRequest*);
  } g_handlers[] = {
      {string_static("initialize"), lsp_handle_req_initialize},
      {string_static("shutdown"), lsp_handle_req_shutdown},
  };

  for (u32 i = 0; i != array_elems(g_handlers); ++i) {
    if (string_eq(req->method, g_handlers[i].method)) {
      g_handlers[i].handler(ctx, req);
      return;
    }
  }
  lsp_send_response_error(ctx, req, &g_jrpcErrorMethodNotFound);
}

static void lsp_handle_jrpc(LspContext* ctx, const JsonVal value) {
  if (UNLIKELY(json_type(ctx->jsonDoc, value) != JsonType_Object)) {
    ctx->status = LspStatus_ErrorInvalidJRpcMessage;
    return;
  }
  const String version = lsp_maybe_str(ctx, json_field(ctx->jsonDoc, value, string_lit("jsonrpc")));
  if (UNLIKELY(!string_eq(version, string_lit("2.0")))) {
    ctx->status = LspStatus_ErrorUnsupportedJRpcVersion;
    return;
  }
  const String method = lsp_maybe_str(ctx, json_field(ctx->jsonDoc, value, string_lit("method")));
  if (UNLIKELY(string_is_empty(method))) {
    ctx->status = LspStatus_ErrorInvalidJRpcMessage;
    return;
  }
  const JsonVal params = json_field(ctx->jsonDoc, value, string_lit("params"));
  const JsonVal id     = json_field(ctx->jsonDoc, value, string_lit("id"));

  if (sentinel_check(id)) {
    lsp_handle_notif(ctx, &(JRpcNotification){.method = method, .params = params});
  } else {
    lsp_handle_req(ctx, &(JRpcRequest){.method = method, .params = params, .id = id});
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

  if (ctx.status != LspStatus_Exit) {
    const String errorMsg = g_lspStatusMessage[ctx.status];
    file_write_sync(g_file_stderr, fmt_write_scratch("lsp: {}\n", fmt_text(errorMsg)));
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

  file_write_sync(g_file_stderr, string_lit("lsp: No communication method specified.\n"));
  return 1;
}
