#include "core_file.h"
#include "core_init.h"

#include "cli.h"

#include "log.h"

#include "jobs_init.h"

#include "check_runner.h"

static int run_tests(const bool outputPassingTests) {
  CheckDef* check = check_create(g_alloc_heap);

  register_spec(check, alloc_bump);
  register_spec(check, alloc_page);
  register_spec(check, alloc_scratch);
  register_spec(check, ascii);
  register_spec(check, base64);
  register_spec(check, bits);
  register_spec(check, bitset);
  register_spec(check, compare);
  register_spec(check, dynarray);
  register_spec(check, dynbitset);
  register_spec(check, dynstring);
  register_spec(check, env);
  register_spec(check, file);
  register_spec(check, float);
  register_spec(check, format);
  register_spec(check, macro);
  register_spec(check, math);
  register_spec(check, memory);
  register_spec(check, path);
  register_spec(check, rng);
  register_spec(check, shuffle);
  register_spec(check, sort);
  register_spec(check, string);
  register_spec(check, thread);
  register_spec(check, time);
  register_spec(check, utf8);
  register_spec(check, winutils);

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

  CliApp* app = cli_app_create(g_alloc_heap, string_lit("Test harness for the volo core library."));

  const CliId outputPassingTestsFlag =
      cli_register_flag(app, 'o', string_lit("output-passing"), CliOptionFlags_None);
  cli_register_desc(app, outputPassingTestsFlag, string_lit("Display passing tests."));

  const CliId helpFlag = cli_register_flag(app, 'h', string_lit("help"), CliOptionFlags_None);
  cli_register_desc(app, helpFlag, string_lit("Display this help page."));

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
