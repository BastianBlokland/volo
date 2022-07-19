#include "app_cli.h"
#include "core_alloc.h"
#include "core_file.h"
#include "core_init.h"
#include "jobs_init.h"
#include "log_init.h"

/**
 * Entrypoint for cli applications.
 */
int main(const int argc, const char** argv) {
  core_init();
  jobs_init();
  log_init();

  int exitCode = 0;

  CliApp* app = cli_app_create(g_alloc_heap);
  app_cli_configure(app);

  CliInvocation* invoc = cli_parse(app, argc - 1, argv + 1);
  if (cli_parse_result(invoc) == CliParseResult_Fail) {
    cli_failure_write_file(invoc, g_file_stderr);
    exitCode = 2;
    goto exit;
  }

  exitCode = app_cli_run(app, invoc);

exit:
  cli_parse_destroy(invoc);
  cli_app_destroy(app);

  log_teardown();
  jobs_teardown();
  core_teardown();
  return exitCode;
}
