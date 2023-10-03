#include "app_cli.h"
#include "core_file.h"
#include "core_format.h"
#include "core_path.h"

/**
 * Language Server Protocol implementation for the Volo script language.
 */

static i32 lsp_run_stdio() {
  //
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

  const String appName = path_filename(g_path_executable);
  file_write_sync(
      g_file_stderr,
      fmt_write_scratch("{}: No communication method specified.\n", fmt_text(appName)));
  return 1;
}
