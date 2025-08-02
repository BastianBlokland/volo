#include "app_cli.h"
#include "cli_app.h"
#include "cli_failure.h"
#include "cli_parse.h"
#include "core_alloc.h"
#include "core_diag_except.h"
#include "core_file.h"
#include "core_init.h"
#include "core_symbol.h"
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

  const CliId optDbgSyms =
      cli_register_flag(app, '\0', string_lit("debug-symbols"), CliOptionFlags_Exclusive);
  cli_register_desc(app, optDbgSyms, string_lit("Dump a listing of all debug symbols."));

  CliInvocation* invoc = cli_parse(app, argc - 1, argv + 1);
  if (cli_parse_result(invoc) == CliParseResult_Fail) {
    cli_failure_write_file(invoc, g_fileStdErr);
    exitCode = 2;
    goto exit;
  }
  if (cli_parse_provided(invoc, optDbgSyms)) {
    if (!symbol_dbg_dump(g_fileStdOut)) {
      file_write_sync(g_fileStdErr, string_lit("No debug symbols found.\n"));
      exitCode = 1;
    }
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
