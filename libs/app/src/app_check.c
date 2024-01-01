#include "app_check.h"
#include "app_cli.h"
#include "check_def.h"
#include "check_runner.h"
#include "core_alloc.h"
#include "core_file.h"
#include "jobs_init.h"
#include "log.h"

static CliId g_optOutputPassingTests, g_optHelp;

static CheckRunFlags app_check_runflags(const CliInvocation* invoc) {
  CheckRunFlags flags = CheckRunFlags_None;
  if (cli_parse_provided(invoc, g_optOutputPassingTests)) {
    flags |= CheckRunFlags_OutputPassingTests;
  }
  return flags;
}

void app_cli_configure(CliApp* app) {
  g_optOutputPassingTests =
      cli_register_flag(app, 'o', string_lit("output-passing"), CliOptionFlags_None);
  cli_register_desc(app, g_optOutputPassingTests, string_lit("Display passing tests."));

  g_optHelp = cli_register_flag(app, 'h', string_lit("help"), CliOptionFlags_None);
  cli_register_desc(app, g_optHelp, string_lit("Display this help page."));
  cli_register_exclusion(app, g_optHelp, g_optOutputPassingTests);
}

i32 app_cli_run(const CliApp* app, const CliInvocation* invoc) {
  jobs_init();

  i32 exitCode = 0;
  log_add_sink(g_logger, log_sink_json_default(g_alloc_heap, LogMask_All));

  if (cli_parse_provided(invoc, g_optHelp)) {
    cli_help_write_file(app, g_file_stdout);
    goto Exit;
  }

  CheckDef* check = check_create(g_alloc_heap);
  app_check_configure(check);

  if (check_run(check, app_check_runflags(invoc))) {
    exitCode = 1; // Tests failed.
  }

  check_destroy(check);

Exit:
  jobs_teardown();
  return exitCode;
}
