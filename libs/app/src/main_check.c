#include "app_check.h"
#include "check_def.h"
#include "check_runner.h"
#include "cli.h"
#include "core.h"
#include "core_file.h"
#include "jobs.h"
#include "log.h"

static CliId g_outputPassingTestsFlag, g_helpFlag;

static void app_check_cli_configure(CliApp* app) {
  g_outputPassingTestsFlag =
      cli_register_flag(app, 'o', string_lit("output-passing"), CliOptionFlags_None);
  cli_register_desc(app, g_outputPassingTestsFlag, string_lit("Display passing tests."));

  g_helpFlag = cli_register_flag(app, 'h', string_lit("help"), CliOptionFlags_None);
  cli_register_desc(app, g_helpFlag, string_lit("Display this help page."));
  cli_register_exclusion(app, g_helpFlag, g_outputPassingTestsFlag);
}

static CheckRunFlags app_check_runflags(const CliInvocation* invoc) {
  CheckRunFlags flags = CheckRunFlags_None;
  if (cli_parse_provided(invoc, g_outputPassingTestsFlag)) {
    flags |= CheckRunFlags_OutputPassingTests;
  }
  return flags;
}

/**
 * Entrypoint for check (unit test library) applications.
 */
int main(const int argc, const char** argv) {
  core_init();
  jobs_init();
  log_init();

  log_add_sink(g_logger, log_sink_json_default(g_alloc_heap, LogMask_All));

  int exitCode = 0;

  CliApp* app = cli_app_create(g_alloc_heap);
  app_check_cli_configure(app);

  CliInvocation* invoc = cli_parse(app, argc - 1, argv + 1);
  if (cli_parse_result(invoc) == CliParseResult_Fail) {
    cli_failure_write_file(invoc, g_file_stderr);
    exitCode = 2; // Invalid arguments provided to app.
    goto exit;
  }

  if (cli_parse_provided(invoc, g_helpFlag)) {
    cli_help_write_file(app, g_file_stdout);
    goto exit;
  }

  CheckDef* check = check_create(g_alloc_heap);
  app_check_configure(check);

  if (check_run(check, app_check_runflags(invoc))) {
    exitCode = 1; // Tests failed.
  }

  check_destroy(check);

exit:
  cli_parse_destroy(invoc);
  cli_app_destroy(app);

  log_teardown();
  jobs_teardown();
  core_teardown();
  return exitCode;
}
