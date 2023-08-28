#include "app_cli.h"
#include "core_alloc.h"
#include "core_file.h"
#include "core_format.h"
#include "core_tty.h"

/**
 * ReadEvalPrintLoop - Utility to play around with script execution.
 */

static i32 repl_run_interactive() {
  if (!tty_isatty(g_file_stdin) || !tty_isatty(g_file_stdout)) {
    file_write_sync(g_file_stderr, string_lit("ERROR: REPL has to be ran interactively\n"));
    return 1;
  }
  tty_opts_set(g_file_stdin, TtyOpts_NoEcho | TtyOpts_NoBuffer | TtyOpts_NoSignals);

  DynString readBuffer = dynstring_create(g_alloc_heap, 512);

  while (tty_read(g_file_stdin, &readBuffer, TtyReadFlags_None)) {
    const String input = dynstring_view(&readBuffer);
    if (string_eq(input, string_lit("\03"))) {
      break;
    }
    file_write_sync(
        g_file_stdout,
        fmt_write_scratch("> {}\n", fmt_text(input, .flags = FormatTextFlags_EscapeNonPrintAscii)));
    dynstring_clear(&readBuffer);
  }
  dynstring_destroy(&readBuffer);

  tty_opts_set(g_file_stdin, TtyOpts_None);
  return 0;
}

static CliId g_helpFlag;

void app_cli_configure(CliApp* app) {
  cli_app_register_desc(app, string_lit("Script ReadEvalPrintLoop utility."));

  g_helpFlag = cli_register_flag(app, 'h', string_lit("help"), CliOptionFlags_None);
  cli_register_desc(app, g_helpFlag, string_lit("Display this help page."));
}

i32 app_cli_run(const CliApp* app, const CliInvocation* invoc) {
  if (cli_parse_provided(invoc, g_helpFlag)) {
    cli_help_write_file(app, g_file_stdout);
    return 0;
  }
  return repl_run_interactive();
}
