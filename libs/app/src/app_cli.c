#include "app_cli.h"
#include "cli_app.h"
#include "cli_failure.h"
#include "cli_parse.h"
#include "core_alloc.h"
#include "core_diag_except.h"
#include "core_file.h"
#include "core_init.h"
#include "data_init.h"
#include "log_init.h"

int SYS_DECL main(const int argc, const char** argv) {
  core_init();

  jmp_buf exceptAnchor;
  diag_except_enable(&exceptAnchor, setjmp(exceptAnchor));

  log_init();
  data_init();

  int exitCode = 0;

  CliApp* app = cli_app_create(g_allocHeap);
  app_cli_configure(app);

  CliInvocation* invoc = cli_parse(app, argc - 1, argv + 1);
  if (cli_parse_result(invoc) == CliParseResult_Fail) {
    cli_failure_write_file(invoc, g_fileStdErr);
    exitCode = 2;
    goto exit;
  }

  exitCode = app_cli_run(app, invoc);

exit:
  cli_parse_destroy(invoc);
  cli_app_destroy(app);

  data_teardown();
  log_teardown();

  diag_except_disable();

  core_teardown();
  return exitCode;
}
