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
  ServerStatus_Running,
  ServerStatus_Shutdown,
  ServerStatus_ErrorReadFailed,
  ServerStatus_ErrorInvalidJson,

  ServerStatus_Count,
} ServerStatus;

static const String g_serverStatusMessage[ServerStatus_Count] = {
    [ServerStatus_Running]          = string_static("Running"),
    [ServerStatus_Shutdown]         = string_static("Shutdown"),
    [ServerStatus_ErrorReadFailed]  = string_static("Error: Read failed"),
    [ServerStatus_ErrorInvalidJson] = string_static("Error: Invalid json received"),
};

typedef struct {
  ServerStatus status;
  DynString*   readBuffer;
  File*        in;
  File*        out;
} ServerContext;

typedef struct {
  usize contentLength;
} LspHeader;

static void lsp_write_err(const String msg) {
  const String appName = path_filename(g_path_executable);
  const String text    = fmt_write_scratch("{}: {}\n", fmt_text(appName), fmt_text(msg));
  file_write_sync(g_file_stderr, text);
}

static void lsp_read_chunk(ServerContext* ctx) {
  const FileResult res = file_read_sync(ctx->in, ctx->readBuffer);
  if (UNLIKELY(res != FileResult_Success)) {
    ctx->status = ServerStatus_ErrorReadFailed;
  }
}

static String lsp_read_until(ServerContext* ctx, const String pattern) {
  while (LIKELY(ctx->status == ServerStatus_Running)) {
    const String text = dynstring_view(ctx->readBuffer);
    const usize  pos  = string_find_first(text, pattern);
    if (!sentinel_check(pos)) {
      dynstring_erase_chars(ctx->readBuffer, 0, pos + pattern.size);
      return string_slice(text, 0, pos + pattern.size);
    }
    lsp_read_chunk(ctx);
  }
  return string_empty;
}

static String lsp_read_sized(ServerContext* ctx, const usize size) {
  while (LIKELY(ctx->status == ServerStatus_Running)) {
    const String text = dynstring_view(ctx->readBuffer);
    if (text.size >= size) {
      dynstring_erase_chars(ctx->readBuffer, 0, size);
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

static LspHeader lsp_read_header(ServerContext* ctx) {
  LspHeader result = {0};
  String    input  = lsp_read_until(ctx, string_lit("\r\n\r\n"));
  while (LIKELY(ctx->status == ServerStatus_Running)) {
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

static void lsp_handle_jrpc(ServerContext* ctx, JsonDoc* jsonDoc, const JsonVal value) {
  (void)ctx;
  (void)jsonDoc;
  (void)value;
}

static i32 lsp_run_stdio() {
  DynString readBuffer  = dynstring_create(g_alloc_heap, 8 * usize_kibibyte);
  JsonDoc*  contentJson = json_create(g_alloc_heap, 1024);

  ServerContext ctx = {
      .status     = ServerStatus_Running,
      .readBuffer = &readBuffer,
      .in         = g_file_stdin,
      .out        = g_file_stdout,
  };

  while (LIKELY(ctx.status == ServerStatus_Running)) {
    const LspHeader header  = lsp_read_header(&ctx);
    const String    content = lsp_read_sized(&ctx, header.contentLength);

    JsonResult jsonResult;
    json_clear(contentJson);
    json_read(contentJson, content, &jsonResult);
    if (UNLIKELY(jsonResult.type == JsonResultType_Fail)) {
      const String jsonErrMsg = json_error_str(jsonResult.error);
      lsp_write_err(fmt_write_scratch("Json read failed: {}", fmt_text(jsonErrMsg)));
      ctx.status = ServerStatus_ErrorInvalidJson;
      break;
    }

    lsp_handle_jrpc(&ctx, contentJson, jsonResult.val);
  }

  json_destroy(contentJson);
  dynstring_destroy(&readBuffer);

  if (ctx.status != ServerStatus_Shutdown) {
    lsp_write_err(g_serverStatusMessage[ctx.status]);
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

  lsp_write_err(string_lit("No communication method specified."));
  return 1;
}
