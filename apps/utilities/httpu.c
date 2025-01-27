#include "app_cli.h"
#include "cli_app.h"
#include "cli_help.h"
#include "cli_parse.h"
#include "core_alloc.h"
#include "core_dynstring.h"
#include "core_file.h"
#include "log_logger.h"
#include "log_sink_json.h"
#include "log_sink_pretty.h"
#include "net_http.h"

/**
 * HttpUtility - Utility to test the http client.
 */

static CliId g_optVerbose, g_optHelp;

void app_cli_configure(CliApp* app) {
  cli_app_register_desc(app, string_lit("Http Utility."));

  g_optVerbose = cli_register_flag(app, 'v', string_lit("verbose"), CliOptionFlags_None);
  cli_register_desc(app, g_optVerbose, string_lit("Enable diagnostic output."));

  g_optHelp = cli_register_flag(app, 'h', string_lit("help"), CliOptionFlags_None);
  cli_register_desc(app, g_optHelp, string_lit("Display this help page."));
  cli_register_exclusions(app, g_optHelp, g_optVerbose);
}

i32 app_cli_run(const CliApp* app, const CliInvocation* invoc) {
  if (cli_parse_provided(invoc, g_optHelp)) {
    cli_help_write_file(app, g_fileStdOut);
    return 0;
  }

  if (cli_parse_provided(invoc, g_optVerbose)) {
    log_add_sink(g_logger, log_sink_pretty_default(g_allocHeap, LogMask_All));
  }
  log_add_sink(g_logger, log_sink_json_default(g_allocHeap, LogMask_All));

  return 0;
}
