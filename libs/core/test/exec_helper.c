#include "app_cli.h"
#include "core_file.h"

/**
 * Helper executable that is used in the process tests.
 */

static CliId g_optExitCode;

void app_cli_configure(CliApp* app) {
  g_optExitCode = cli_register_flag(app, 0, string_lit("exitcode"), CliOptionFlags_Value);
}

i32 app_cli_run(const CliApp* app, const CliInvocation* invoc) {
  (void)app;

  return (i32)cli_read_i64(invoc, g_optExitCode, 0);
}
