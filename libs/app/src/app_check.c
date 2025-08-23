#include "app/check.h"
#include "app/cli.h"
#include "check/def.h"
#include "check/runner.h"
#include "cli/app.h"
#include "cli/parse.h"
#include "cli/read.h"
#include "core/alloc.h"
#include "core/file.h"
#include "jobs/init.h"
#include "log/logger.h"
#include "log/sink_json.h"
#include "trace/dump.h"
#include "trace/init.h"
#include "trace/sink_store.h"
#include "trace/tracer.h"

static CliId g_optOutputPassingTests, g_optJobWorkers;

static CheckRunFlags app_check_runflags(const CliInvocation* invoc) {
  CheckRunFlags flags = CheckRunFlags_None;
  if (cli_parse_provided(invoc, g_optOutputPassingTests)) {
    flags |= CheckRunFlags_OutputPassingTests;
  }
  return flags;
}

AppType app_cli_configure(CliApp* app) {
  g_optOutputPassingTests = cli_register_flag(app, 'o', string_lit("output-passing"), 0);
  cli_register_desc(app, g_optOutputPassingTests, string_lit("Display passing tests."));

  g_optJobWorkers = cli_register_flag(app, '\0', string_lit("workers"), CliOptionFlags_Value);
  cli_register_desc(app, g_optJobWorkers, string_lit("Amount of job workers."));

  return AppType_Console;
}

i32 app_cli_run(MAYBE_UNUSED const CliApp* app, const CliInvocation* invoc) {
  trace_init();

  i32 exitCode = 0;
  log_add_sink(g_logger, log_sink_json_default(g_allocHeap, LogMask_All));

#ifdef VOLO_TRACE
  TraceSink* traceStore = trace_sink_store(g_allocHeap);
  trace_add_sink(g_tracer, traceStore);
#endif

  const JobsConfig jobsConfig = {
      .workerCount = (u16)cli_read_u64(invoc, g_optJobWorkers, 0),
  };
  jobs_init(&jobsConfig);

  CheckDef* check = check_create(g_allocHeap);
  app_check_init(check);

  if (check_run(check, app_check_runflags(invoc))) {
    exitCode = 1; // Tests failed.
  }

#ifdef VOLO_TRACE
  trace_dump_eventtrace_to_path_default(traceStore);
#endif

  app_check_teardown();
  check_destroy(check);

  jobs_teardown();
  trace_teardown();
  return exitCode;
}
