#include "app_cli.h"
#include "core_file.h"

/**
 * Helper executable that is used in the process tests.
 */

void app_cli_configure(CliApp* app) { (void)app; }

i32 app_cli_run(const CliApp* app, const CliInvocation* invoc) {
  (void)app;
  (void)invoc;

  file_write_sync(g_file_stdout, string_lit("Hello World\n"));
  return 0;
}
