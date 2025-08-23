#include "app/cli.h"
#include "cli/app.h"
#include "cli/failure.h"
#include "cli/help.h"
#include "cli/parse.h"
#include "core/alloc.h"
#include "core/diag_except.h"
#include "core/file.h"
#include "core/format.h"
#include "core/init.h"
#include "core/symbol.h"
#include "core/version.h"
#include "data/init.h"
#include "log/init.h"

#ifdef VOLO_WIN32

#include "core/winutils.h"

usize wcslen(const wchar_t*);

static CliInvocation* app_cli_parse(const CliApp* app, const int argc, const wchar_t** argv) {
  // NOTE: Skip the first argument as it is expected to contain the program path.
  const u32 valueCount = argc > 0 ? (argc - 1) : 0;
  String*   values     = mem_stack(sizeof(String) * valueCount).ptr;
  for (u32 i = 0; i != valueCount; ++i) {
    const usize argLen = wcslen(argv[i + 1]);
    values[i]          = argLen ? winutils_from_widestr_scratch(argv[i + 1], argLen) : string_empty;
  }
  return cli_parse(app, values, valueCount);
}

#else // !VOLO_WIN32

static CliInvocation* app_cli_parse(const CliApp* app, const int argc, const char** argv) {
  // NOTE: Skip the first argument as it is expected to contain the program path.
  const u32 valueCount = argc > 0 ? (argc - 1) : 0;
  String*   values     = mem_stack(sizeof(String) * valueCount).ptr;
  for (u32 i = 0; i != valueCount; ++i) {
    values[i] = string_from_null_term(argv[i + 1]);
  }
  return cli_parse(app, values, valueCount);
}

#endif // !VOLO_WIN32

#ifdef VOLO_WIN32
int SYS_DECL wmain(const int argc, const wchar_t** argv) {
#else
int SYS_DECL main(const int argc, const char** argv) {
#endif
  core_init();

  jmp_buf exceptAnchor;
  diag_except_enable(&exceptAnchor, setjmp(exceptAnchor));

  log_init();
  data_init();

  int exitCode = 0;

  CliApp*       app     = cli_app_create(g_allocHeap);
  const AppType appType = app_cli_configure(app);

  const CliId optDbgSyms =
      cli_register_flag(app, '\0', string_lit("debug-symbols"), CliOptionFlags_Exclusive);
  cli_register_desc(app, optDbgSyms, string_lit("Dump a listing of all debug symbols."));

  CliId optConsole, optNoConsole;
  if (appType == AppType_Gui) {
    optConsole = cli_register_flag(app, '\0', string_lit("console"), CliOptionFlags_None);
    cli_register_desc(app, optConsole, string_lit("Require console input / output."));

    optNoConsole = cli_register_flag(app, '\0', string_lit("no-console"), CliOptionFlags_None);
    cli_register_desc(app, optNoConsole, string_lit("Disable console input / output."));

    cli_register_exclusion(app, optConsole, optNoConsole);
  }

  const CliId optVer = cli_register_flag(app, 'v', string_lit("version"), CliOptionFlags_Exclusive);
  cli_register_desc(app, optVer, string_lit("Output the executable version."));

  const CliId optHelp = cli_register_flag(app, 'h', string_lit("help"), CliOptionFlags_Exclusive);
  cli_register_desc(app, optHelp, string_lit("Output this help page."));

  CliInvocation* invoc = app_cli_parse(app, argc, argv);
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
  if (cli_parse_provided(invoc, optVer)) {
    const String exeVerStr = version_str_scratch(g_versionExecutable);
    file_write_sync(g_fileStdOut, fmt_write_scratch("v{}\n", fmt_text(exeVerStr)));
    goto exit;
  }
  if (cli_parse_provided(invoc, optHelp)) {
    cli_help_write_file(app, CliHelpFlags_IncludeVersion, g_fileStdOut);
    goto exit;
  }
  if (appType == AppType_Gui) {
    /**
     * Close the standard file handles (stdIn, stdOut, stdErr) if they are not in use.
     * On Windows this closes the console if launched from another Gui application (eg explorer).
     */
    bool closeStdHandles = file_std_unused();
    if (cli_parse_provided(invoc, optNoConsole)) {
      closeStdHandles = true;
    }
    if (cli_parse_provided(invoc, optConsole)) {
      closeStdHandles = false;
    }
    if (closeStdHandles) {
      file_std_close();
    }
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
