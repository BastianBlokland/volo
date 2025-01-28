#include "app_cli.h"
#include "cli_app.h"
#include "cli_help.h"
#include "cli_parse.h"
#include "cli_read.h"
#include "core_alloc.h"
#include "core_dynstring.h"
#include "core_file.h"
#include "log_logger.h"
#include "log_sink_json.h"
#include "log_sink_pretty.h"
#include "net_http.h"
#include "net_result.h"

/**
 * HttpUtility - Utility to test the http client.
 */

typedef struct {
  String host;
  String uri;        // Optional.
  String outputPath; // Optional.
} AppContext;

static i32 httpu_get(const AppContext* ctx) {
  i32       res    = 0;
  NetHttp*  client = net_http_connect_sync(g_allocHeap, ctx->host, NetHttpFlags_None);
  DynString buffer = dynstring_create(g_allocHeap, 16 * usize_kibibyte);

  if (net_http_status(client) != NetResult_Success) {
    res = 1;
    goto Done;
  }

  const NetResult getResult = net_http_get_sync(client, ctx->uri, &buffer);
  if (getResult != NetResult_Success) {
    res = 1;
    goto Done;
  }

  if (string_is_empty(ctx->outputPath)) {
    file_write_sync(g_fileStdOut, dynstring_view(&buffer));
  } else {
    file_write_to_path_sync(ctx->outputPath, dynstring_view(&buffer));
  }

Done:
  dynstring_destroy(&buffer);
  net_http_shutdown_sync(client);
  net_http_destroy(client);
  return res;
}

static CliId g_optHost, g_optUri, g_optOutput, g_optHelp;

void app_cli_configure(CliApp* app) {
  cli_app_register_desc(app, string_lit("Http Utility."));

  g_optHost = cli_register_arg(app, string_lit("host"), CliOptionFlags_Required);
  cli_register_desc(app, g_optUri, string_lit("Target host."));

  g_optUri = cli_register_arg(app, string_lit("uri"), CliOptionFlags_Value);
  cli_register_desc(app, g_optUri, string_lit("Target uri."));

  g_optOutput = cli_register_flag(app, 'o', string_lit("output"), CliOptionFlags_Value);
  cli_register_desc(app, g_optOutput, string_lit("Output file path."));

  g_optHelp = cli_register_flag(app, 'h', string_lit("help"), CliOptionFlags_None);
  cli_register_desc(app, g_optHelp, string_lit("Display this help page."));
  cli_register_exclusions(app, g_optHelp, g_optHost);
  cli_register_exclusions(app, g_optHelp, g_optUri);
  cli_register_exclusions(app, g_optHelp, g_optOutput);
}

i32 app_cli_run(const CliApp* app, const CliInvocation* invoc) {
  if (cli_parse_provided(invoc, g_optHelp)) {
    cli_help_write_file(app, g_fileStdOut);
    return 0;
  }

  log_add_sink(g_logger, log_sink_pretty_default(g_allocHeap, LogMask_All));
  log_add_sink(g_logger, log_sink_json_default(g_allocHeap, LogMask_All));

  AppContext ctx = {
      .host       = cli_read_string(invoc, g_optHost, string_empty),
      .uri        = cli_read_string(invoc, g_optUri, string_empty),
      .outputPath = cli_read_string(invoc, g_optOutput, string_empty),
  };

  return httpu_get(&ctx);
}
