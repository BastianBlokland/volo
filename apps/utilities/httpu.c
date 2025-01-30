#include "app_cli.h"
#include "cli_app.h"
#include "cli_help.h"
#include "cli_parse.h"
#include "cli_read.h"
#include "core_alloc.h"
#include "core_diag.h"
#include "core_dynstring.h"
#include "core_file.h"
#include "log_logger.h"
#include "log_sink_json.h"
#include "log_sink_pretty.h"
#include "net_http.h"
#include "net_init.h"
#include "net_result.h"

/**
 * HttpUtility - Utility to test the http client.
 */

typedef enum {
  HttpuProtocol_Http,
  HttpuProtocol_Https,

  HttpuProtocol_Count,
} HttpuProtocol;

static const String g_protocolStrs[] = {
    string_static("http"),
    string_static("https"),
};
ASSERT(array_elems(g_protocolStrs) == HttpuProtocol_Count, "Incorrect number of protocol strings");

static bool httpu_validate_protocol(const String input) {
  array_for_t(g_protocolStrs, String, protocol) {
    if (string_eq(*protocol, input)) {
      return true;
    }
  }
  return false;
}

typedef enum {
  HttpuMethod_Head,
  HttpuMethod_Get,

  HttpuMethod_Count,
} HttpuMethod;

static const String g_methodStrs[] = {
    string_static("head"),
    string_static("get"),
};
ASSERT(array_elems(g_methodStrs) == HttpuMethod_Count, "Incorrect number of method strings");

static bool httpu_validate_method(const String input) {
  array_for_t(g_methodStrs, String, method) {
    if (string_eq(*method, input)) {
      return true;
    }
  }
  return false;
}

typedef struct {
  HttpuProtocol protocol;
  HttpuMethod   method;
  String        host;
  String        uri;        // Optional.
  String        outputPath; // Optional.
  NetHttpAuth   auth;
} HttpuContext;

static NetHttpFlags httpu_flags(const HttpuContext* ctx) {
  switch (ctx->protocol) {
  case HttpuProtocol_Http:
    return NetHttpFlags_None;
  case HttpuProtocol_Https:
    /**
     * Enable Tls transport but do not enable certificate validation.
     * This means traffic is encrypted and people cannot eavesdrop, however its trivial for someone
     * to man-in-the-middle as we do not verify the server's authenticity.
     * Please do not use this for security sensitive applications!
     */
    return NetHttpFlags_TlsNoVerify;
  case HttpuProtocol_Count:
    break;
  }
  diag_crash_msg("Unsupported protocol");
}

static i32 httpu_head(const HttpuContext* ctx) {
  i32      res    = 0;
  NetHttp* client = net_http_connect_sync(g_allocHeap, ctx->host, httpu_flags(ctx));

  if (net_http_status(client) != NetResult_Success) {
    res = 1;
    goto Done;
  }
  NetHttpEtag etag = {0};
  if (net_http_head_sync(client, ctx->uri, &ctx->auth, &etag) != NetResult_Success) {
    res = 1;
    goto Done;
  }

Done:
  net_http_shutdown_sync(client);
  net_http_destroy(client);
  return res;
}

static i32 httpu_get(const HttpuContext* ctx) {
  i32       res    = 0;
  NetHttp*  client = net_http_connect_sync(g_allocHeap, ctx->host, httpu_flags(ctx));
  DynString buffer = dynstring_create(g_allocHeap, 16 * usize_kibibyte);

  if (net_http_status(client) != NetResult_Success) {
    res = 1;
    goto Done;
  }
  NetHttpEtag     etag      = {0};
  const NetResult getResult = net_http_get_sync(client, ctx->uri, &ctx->auth, &etag, &buffer);
  if (getResult != NetResult_Success) {
    res = 1;
    goto Done;
  }

  if (string_is_empty(ctx->outputPath)) {
    file_write_sync(g_fileStdOut, dynstring_view(&buffer));
    file_write_sync(g_fileStdOut, string_lit("\n"));
  } else {
    file_write_to_path_sync(ctx->outputPath, dynstring_view(&buffer));
  }

Done:
  dynstring_destroy(&buffer);
  net_http_shutdown_sync(client);
  net_http_destroy(client);
  return res;
}

static CliId g_optHost, g_optUri, g_optOutput, g_optProtocol, g_optMethod;
static CliId g_optUser, g_optPassword;
static CliId g_optHelp;

void app_cli_configure(CliApp* app) {
  cli_app_register_desc(app, string_lit("Http Utility."));

  g_optHost = cli_register_arg(app, string_lit("host"), CliOptionFlags_Required);
  cli_register_desc(app, g_optUri, string_lit("Target host."));

  g_optUri = cli_register_arg(app, string_lit("uri"), CliOptionFlags_Value);
  cli_register_desc(app, g_optUri, string_lit("Target uri."));

  g_optOutput = cli_register_flag(app, 'o', string_lit("output"), CliOptionFlags_Value);
  cli_register_desc(app, g_optOutput, string_lit("Output file path."));

  g_optProtocol = cli_register_flag(app, 'p', string_lit("protocol"), CliOptionFlags_Value);
  cli_register_desc_choice_array(app, g_optProtocol, string_empty, g_protocolStrs, 1 /* https */);
  cli_register_validator(app, g_optProtocol, httpu_validate_protocol);

  g_optMethod = cli_register_flag(app, 'm', string_lit("method"), CliOptionFlags_Value);
  cli_register_desc_choice_array(app, g_optMethod, string_empty, g_methodStrs, 1 /* get */);
  cli_register_validator(app, g_optMethod, httpu_validate_method);

  g_optUser = cli_register_flag(app, 'U', string_lit("user"), CliOptionFlags_Value);
  cli_register_desc(app, g_optUser, string_lit("Http basic auth user."));

  g_optPassword = cli_register_flag(app, 'P', string_lit("password"), CliOptionFlags_Value);
  cli_register_desc(app, g_optPassword, string_lit("Http basic auth password."));

  g_optHelp = cli_register_flag(app, 'h', string_lit("help"), CliOptionFlags_None);
  cli_register_desc(app, g_optHelp, string_lit("Display this help page."));
  cli_register_exclusions(app, g_optHelp, g_optHost);
  cli_register_exclusions(app, g_optHelp, g_optUri);
  cli_register_exclusions(app, g_optHelp, g_optOutput);
  cli_register_exclusions(app, g_optHelp, g_optProtocol);
  cli_register_exclusions(app, g_optHelp, g_optMethod);
  cli_register_exclusions(app, g_optHelp, g_optUser);
  cli_register_exclusions(app, g_optHelp, g_optPassword);
}

i32 app_cli_run(const CliApp* app, const CliInvocation* invoc) {
  if (cli_parse_provided(invoc, g_optHelp)) {
    cli_help_write_file(app, g_fileStdOut);
    return 0;
  }

  if (tty_isatty(g_fileStdOut)) {
    log_add_sink(g_logger, log_sink_pretty_default(g_allocHeap, LogMask_All));
  }
  log_add_sink(g_logger, log_sink_json_default(g_allocHeap, LogMask_All));

  HttpuContext ctx = {
      .protocol   = (HttpuProtocol)cli_read_choice_array(invoc, g_optProtocol, g_protocolStrs, 1),
      .method     = (HttpuMethod)cli_read_choice_array(invoc, g_optMethod, g_methodStrs, 1),
      .host       = cli_read_string(invoc, g_optHost, string_empty),
      .uri        = cli_read_string(invoc, g_optUri, string_empty),
      .outputPath = cli_read_string(invoc, g_optOutput, string_empty),
  };

  if (cli_parse_provided(invoc, g_optUser)) {
    ctx.auth.type = NetHttpAuthType_Basic;
    ctx.auth.user = cli_read_string(invoc, g_optUser, string_empty);
    ctx.auth.pw   = cli_read_string(invoc, g_optPassword, string_empty);
  }

  i32 retCode = 1;

  net_init();
  switch (ctx.method) {
  case HttpuMethod_Head:
    retCode = httpu_head(&ctx);
    break;
  case HttpuMethod_Get:
    retCode = httpu_get(&ctx);
    break;
  case HttpuMethod_Count:
    break;
  }
  net_teardown();

  return retCode;
}
