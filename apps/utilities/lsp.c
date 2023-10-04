#include "app_cli.h"
#include "core_alloc.h"
#include "core_file.h"
#include "core_format.h"
#include "core_path.h"
#include "json.h"

/**
 * Language Server Protocol implementation for the Volo script language.
 */

typedef enum {
  LspStatus_Running,
  LspStatus_Shutdown,
  LspStatus_ErrorReadFailed,
  LspStatus_ErrorInvalidJson,
  LspStatus_ErrorInvalidJRpcMessage,
  LspStatus_ErrorUnsupportedJRpcVersion,

  LspStatus_Count,
} LspStatus;

static const String g_lspStatusMessage[LspStatus_Count] = {
    [LspStatus_Running]                     = string_static("Running"),
    [LspStatus_Shutdown]                    = string_static("Shutdown"),
    [LspStatus_ErrorReadFailed]             = string_static("Error: Read failed"),
    [LspStatus_ErrorInvalidJson]            = string_static("Error: Invalid json received"),
    [LspStatus_ErrorInvalidJRpcMessage]     = string_static("Error: Invalid jrpc message received"),
    [LspStatus_ErrorUnsupportedJRpcVersion] = string_static("Error: Unsupported jrpc version"),
};

typedef struct {
  LspStatus  status;
  DynString* readBuffer;
  usize      readCursor;
  File*      in;
  File*      out;
} LspContext;

typedef struct {
  usize contentLength;
} LspHeader;

typedef struct {
  JsonDoc* doc;
  String   method;
  JsonVal  params; // Optional, sentinel_u32 if unused.
} JRpcNotification;

typedef struct {
  JsonDoc* doc;
  String   method;
  JsonVal  params; // Optional, sentinel_u32 if unused.
  JsonVal  id;
} JRpcRequest;

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

static void lsp_handle_notification(LspContext* ctx, const JRpcNotification* notification) {
  (void)ctx;
  (void)notification;
}

static void lsp_handle_request(LspContext* ctx, const JRpcRequest* request) {
  (void)ctx;
  (void)request;
}

static void lsp_handle_jrpc(LspContext* ctx, JsonDoc* doc, const JsonVal value) {
  if (UNLIKELY(json_type(doc, value) != JsonType_Object)) {
    ctx->status = LspStatus_ErrorInvalidJRpcMessage;
    return;
  }
  const JsonVal version = json_field(doc, value, string_lit("jsonrpc"));
  if (UNLIKELY(sentinel_check(version) || json_type(doc, version) != JsonType_String)) {
    ctx->status = LspStatus_ErrorInvalidJRpcMessage;
    return;
  }
  if (UNLIKELY(!string_eq(json_string(doc, version), string_lit("2.0")))) {
    ctx->status = LspStatus_ErrorUnsupportedJRpcVersion;
    return;
  }
  const JsonVal method = json_field(doc, value, string_lit("method"));
  if (UNLIKELY(sentinel_check(method) || json_type(doc, method) != JsonType_String)) {
    ctx->status = LspStatus_ErrorInvalidJRpcMessage;
    return;
  }
  const JsonVal params = json_field(doc, value, string_lit("params"));
  const JsonVal id     = json_field(doc, value, string_lit("id"));

  if (sentinel_check(id)) {
    const JRpcNotification notification = {
        .doc    = doc,
        .method = json_string(doc, method),
        .params = params,
    };
    lsp_handle_notification(ctx, &notification);
  } else {
    const JRpcRequest request = {
        .doc    = doc,
        .method = json_string(doc, method),
        .params = params,
        .id     = id,
    };
    lsp_handle_request(ctx, &request);
  }
}

static i32 lsp_run_stdio() {
  DynString readBuffer  = dynstring_create(g_alloc_heap, 8 * usize_kibibyte);
  JsonDoc*  contentJson = json_create(g_alloc_heap, 1024);

  LspContext ctx = {
      .status     = LspStatus_Running,
      .readBuffer = &readBuffer,
      .in         = g_file_stdin,
      .out        = g_file_stdout,
  };

  while (LIKELY(ctx.status == LspStatus_Running)) {
    const LspHeader header  = lsp_read_header(&ctx);
    const String    content = lsp_read_sized(&ctx, header.contentLength);

    JsonResult jsonResult;
    json_read(contentJson, content, &jsonResult);
    if (UNLIKELY(jsonResult.type == JsonResultType_Fail)) {
      const String jsonErrMsg = json_error_str(jsonResult.error);
      lsp_output_err(fmt_write_scratch("Json read failed: {}", fmt_text(jsonErrMsg)));
      ctx.status = LspStatus_ErrorInvalidJson;
      break;
    }

    lsp_handle_jrpc(&ctx, contentJson, jsonResult.val);

    json_clear(contentJson);
    lsp_read_trim(&ctx);
  }

  json_destroy(contentJson);
  dynstring_destroy(&readBuffer);

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
