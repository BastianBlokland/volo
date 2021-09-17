#include "check_app.h"
#include "check_runner.h"

#include "cli.h"

typedef struct {
  CliApp* cliApp;
  CliId   outputPassingTestsFlag, helpFlag;
} CheckApp;

static CheckApp check_app_create() {
  CliApp* app = cli_app_create(g_alloc_heap, string_empty);

  const CliId outputPassingTestsFlag =
      cli_register_flag(app, 'o', string_lit("output-passing"), CliOptionFlags_None);
  cli_register_desc(app, outputPassingTestsFlag, string_lit("Display passing tests."));

  const CliId helpFlag = cli_register_flag(app, 'h', string_lit("help"), CliOptionFlags_None);
  cli_register_desc(app, helpFlag, string_lit("Display this help page."));
  cli_register_exclusion(app, helpFlag, outputPassingTestsFlag);

  return (CheckApp){
      .cliApp                 = app,
      .outputPassingTestsFlag = outputPassingTestsFlag,
      .helpFlag               = helpFlag,
  };
}

static void check_app_destroy(CheckApp* app) { cli_app_destroy(app->cliApp); }

static CheckRunFlags check_app_runflags(CheckApp* app, CliInvocation* invoc) {
  CheckRunFlags flags = CheckRunFlags_None;

  if (cli_parse_provided(invoc, app->outputPassingTestsFlag)) {
    flags |= CheckRunFlags_OutputPassingTests;
  }

  return flags;
}

static int check_app_run(CheckApp* app, CheckDef* def, const int argc, const char** argv) {
  int exitCode = 0;

  CliInvocation* invoc = cli_parse(app->cliApp, argc - 1, argv + 1);
  if (cli_parse_result(invoc) == CliParseResult_Fail) {
    cli_failure_write_file(invoc, g_file_stderr);
    exitCode = 2; // Invalid arguments provided to app.
    goto exit;
  }

  if (cli_parse_provided(invoc, app->helpFlag)) {
    cli_help_write_file(app->cliApp, g_file_stdout);
    goto exit;
  }

  if (check_run(def, check_app_runflags(app, invoc))) {
    exitCode = 1; // Tests failed.
  }

exit:
  cli_parse_destroy(invoc);
  return exitCode;
}

int check_app(CheckDef* def, const int argc, const char** argv) {

  CheckApp app = check_app_create();

  const int exitCode = check_app_run(&app, def, argc, argv);

  check_app_destroy(&app);

  return exitCode;
}
