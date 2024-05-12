#include "app_cli.h"
#include "core_alloc.h"
#include "core_diag_except.h"
#include "core_file.h"
#include "core_init.h"
#include "log_init.h"

int SYS_DECL main(const int argc, const char** argv) {
  core_init();
  log_init();

  jmp_buf exceptAnchor;
  diag_except_enable(&exceptAnchor, setjmp(exceptAnchor));

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

  diag_except_disable();

  log_teardown();
  core_teardown();
  return exitCode;
}
