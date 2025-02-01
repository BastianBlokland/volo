#include "app_cli.h"
#include "cli_app.h"
#include "cli_help.h"
#include "cli_parse.h"
#include "cli_read.h"
#include "cli_validate.h"
#include "core_alloc.h"
#include "core_file.h"
#include "log_logger.h"
#include "log_sink_json.h"
#include "log_sink_pretty.h"
#include "net_http.h"
#include "net_init.h"
#include "net_rest.h"
#include "net_result.h"

/**
 * Fetch - Utility to download external assets.
 */

typedef struct {
  String configPath;
} FetchContext;

static i32 fetch_run(FetchContext* ctx) {
  (void)ctx;
  return 0;
}

static CliId g_optConfigPath, g_optHelp;

void app_cli_configure(CliApp* app) {
  cli_app_register_desc(app, string_lit("Fetch utility."));

  g_optConfigPath = cli_register_arg(app, string_lit("config"), CliOptionFlags_Required);
  cli_register_desc(app, g_optConfigPath, string_lit("Path to a fetch config file."));
  cli_register_validator(app, g_optConfigPath, cli_validate_file_regular);

  g_optHelp = cli_register_flag(app, 'h', string_lit("help"), CliOptionFlags_None);
  cli_register_desc(app, g_optHelp, string_lit("Display this help page."));
  cli_register_exclusions(app, g_optHelp, g_optConfigPath);
}

i32 app_cli_run(const CliApp* app, const CliInvocation* invoc) {
  if (cli_parse_provided(invoc, g_optHelp)) {
    cli_help_write_file(app, g_fileStdOut);
    return 0;
  }

  log_add_sink(g_logger, log_sink_pretty_default(g_allocHeap, LogMask_All));
  log_add_sink(g_logger, log_sink_json_default(g_allocHeap, LogMask_All));

  FetchContext ctx = {
      .configPath = cli_read_string(invoc, g_optConfigPath, string_empty),
  };

  i32 retCode;
  net_init();
  retCode = fetch_run(&ctx);
  net_teardown();
  return retCode;
}
