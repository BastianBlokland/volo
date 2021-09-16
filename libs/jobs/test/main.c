#include "core_file.h"
#include "core_init.h"

#include "cli.h"

#include "log.h"

#include "jobs_init.h"

#include "check_runner.h"

static int run_tests(const bool outputPassingTests) {
  CheckDef* check = check_create(g_alloc_heap);

  register_spec(check, dot);
  register_spec(check, executor);
  register_spec(check, graph);
  register_spec(check, scheduler);

  const CheckRunFlags flags =
      outputPassingTests ? CheckRunFlags_OutputPassingTests : CheckRunFlags_None;
  const CheckResultType result = check_run(check, flags);

  check_destroy(check);
  return result;
}

int main(const int argc, const char** argv) {
  core_init();
  jobs_init();
  log_init();

  int exitCode = 0;

  CliApp* app = cli_app_create(g_alloc_heap, string_lit("Test harness for the volo jobs library."));

  const CliId outputPassingTestsFlag =
      cli_register_flag(app, 'o', string_lit("output-passing"), CliOptionFlags_None);
  cli_register_desc(app, outputPassingTestsFlag, string_lit("Display passing tests."));

  const CliId helpFlag = cli_register_flag(app, 'h', string_lit("help"), CliOptionFlags_None);
  cli_register_desc(app, helpFlag, string_lit("Display this help page."));
  cli_register_exclusion(app, helpFlag, outputPassingTestsFlag);

  CliInvocation* invoc = cli_parse(app, argc - 1, argv + 1);
  if (cli_parse_result(invoc) == CliParseResult_Fail) {
    cli_failure_write_file(invoc, g_file_stderr);
    exitCode = 2;
    goto exit;
  }

  if (cli_parse_provided(invoc, helpFlag)) {
    cli_help_write_file(app, g_file_stdout);
    goto exit;
  }

  log_add_sink(g_logger, log_sink_json_default(g_alloc_heap, LogMask_All));

  const bool outputPassingTests = cli_parse_provided(invoc, outputPassingTestsFlag);
  exitCode                      = run_tests(outputPassingTests);

exit:
  cli_parse_destroy(invoc);
  cli_app_destroy(app);

  log_teardown();
  jobs_teardown();
  core_teardown();
  return exitCode;
}
