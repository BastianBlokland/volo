#include "app_cli.h"
#include "core_file.h"

/**
 * ReadEvalPrintLoop - Utility to play around with script execution.
 */

static CliId g_helpFlag;

void app_cli_configure(CliApp* app) {
  cli_app_register_desc(app, string_lit("Script ReadEvalPrintLoop utility."));

  g_helpFlag = cli_register_flag(app, 'h', string_lit("help"), CliOptionFlags_None);
  cli_register_desc(app, g_helpFlag, string_lit("Display this help page."));
}

i32 app_cli_run(const CliApp* app, const CliInvocation* invoc) {
  if (cli_parse_provided(invoc, g_helpFlag)) {
    cli_help_write_file(app, g_file_stdout);
    return 0;
  }

  file_write_sync(g_file_stdout, string_lit("Hello world\n"));
  return 0;
}
