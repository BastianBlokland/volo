#include "app_check.h"
#include "app_cli.h"
#include "check_def.h"
#include "check_runner.h"
#include "cli_app.h"
#include "cli_help.h"
#include "cli_parse.h"
#include "cli_read.h"
#include "core_alloc.h"
#include "core_file.h"
#include "jobs_init.h"
#include "log_logger.h"
#include "log_sink_json.h"
#include "trace_init.h"

static CliId g_optOutputPassingTests, g_optJobWorkers, g_optHelp;

static CheckRunFlags app_check_runflags(const CliInvocation* invoc) {
  CheckRunFlags flags = CheckRunFlags_None;
  if (cli_parse_provided(invoc, g_optOutputPassingTests)) {
    flags |= CheckRunFlags_OutputPassingTests;
  }
  return flags;
}

void app_cli_configure(CliApp* app) {
  g_optOutputPassingTests = cli_register_flag(app, 'o', string_lit("output-passing"), 0);
  cli_register_desc(app, g_optOutputPassingTests, string_lit("Display passing tests."));

  g_optJobWorkers = cli_register_flag(app, '\0', string_lit("workers"), CliOptionFlags_Value);
  cli_register_desc(app, g_optJobWorkers, string_lit("Amount of job workers."));

  g_optHelp = cli_register_flag(app, 'h', string_lit("help"), 0);
  cli_register_desc(app, g_optHelp, string_lit("Display this help page."));
  cli_register_exclusion(app, g_optHelp, g_optOutputPassingTests);
}

i32 app_cli_run(const CliApp* app, const CliInvocation* invoc) {
  trace_init();

  i32 exitCode = 0;
  log_add_sink(g_logger, log_sink_json_default(g_allocHeap, LogMask_All));

  if (cli_parse_provided(invoc, g_optHelp)) {
    cli_help_write_file(app, g_fileStdOut);
    goto Exit;
  }

  const JobsConfig jobsConfig = {
      .workerCount = (u16)cli_read_u64(invoc, g_optJobWorkers, 0),
  };
  jobs_init(&jobsConfig);

  CheckDef* check = check_create(g_allocHeap);
  app_check_configure(check);

  if (check_run(check, app_check_runflags(invoc))) {
    exitCode = 1; // Tests failed.
  }

  check_destroy(check);

Exit:
  jobs_teardown();
  trace_teardown();
  return exitCode;
}
