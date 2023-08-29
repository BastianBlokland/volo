#include "app_cli.h"
#include "core_alloc.h"
#include "core_file.h"
#include "core_format.h"
#include "core_tty.h"
#include "core_utf8.h"

/**
 * ReadEvalPrintLoop - Utility to play around with script execution.
 */

typedef struct {
  DynString* editBuffer;
} ReplState;

static void repl_edit_insert(const ReplState* state, const Unicode cp) {
  utf8_cp_write(state->editBuffer, cp);
}

static void repl_edit_delete_char(const ReplState* state) {
  // Delete the last utf8 code-point.
  String str = dynstring_view(state->editBuffer);
  for (usize i = str.size; i-- > 0;) {
    if (!utf8_contchar(*string_at(str, i))) {
      dynstring_erase_chars(state->editBuffer, i, str.size - i);
      return;
    }
  }
}

static void repl_edit_submit(ReplState* state) { dynstring_clear(state->editBuffer); }

static void repl_edit_render(const ReplState* state) {
  Mem       bufferMem = alloc_alloc(g_alloc_scratch, usize_kibibyte, 1);
  DynString buffer    = dynstring_create_over(bufferMem);

  tty_write_clear_line_sequence(&buffer, TtyClearMode_All);
  tty_write_set_cursor_hor_sequence(&buffer, 0);
  dynstring_append(&buffer, dynstring_view(state->editBuffer));

  file_write_sync(g_file_stdout, dynstring_view(&buffer));
  dynstring_destroy(&buffer);
}

static bool repl_update(ReplState* state, TtyInputToken* input) {
  switch (input->type) {
  case TtyInputType_Interrupt:
    return false; // Stop.
  case TtyInputType_Text:
    repl_edit_insert(state, input->val_text);
    break;
  case TtyInputType_KeyBackspace:
    repl_edit_delete_char(state);
    break;
  case TtyInputType_Accept:
    repl_edit_submit(state);
    break;
  default:
    break;
  }
  repl_edit_render(state);
  return true; // Keep running.
}

static i32 repl_run_interactive() {
  if (!tty_isatty(g_file_stdin) || !tty_isatty(g_file_stdout)) {
    file_write_sync(g_file_stderr, string_lit("ERROR: REPL has to be ran interactively\n"));
    return 1;
  }

  DynString readBuffer = dynstring_create(g_alloc_heap, 32);
  DynString editBuffer = dynstring_create(g_alloc_heap, 128);

  ReplState state = {
      .editBuffer = &editBuffer,
  };

  tty_opts_set(g_file_stdin, TtyOpts_NoEcho | TtyOpts_NoBuffer | TtyOpts_NoSignals);

  while (tty_read(g_file_stdin, &readBuffer, TtyReadFlags_None)) {
    String        readStr = dynstring_view(&readBuffer);
    TtyInputToken input;
    for (;;) {
      readStr = tty_input_lex(readStr, &input);
      if (input.type == TtyInputType_End) {
        break;
      }
      if (!repl_update(&state, &input)) {
        goto Stop;
      }
    }
    dynstring_clear(&readBuffer);
  }

Stop:
  tty_opts_set(g_file_stdin, TtyOpts_None);

  dynstring_destroy(&readBuffer);
  dynstring_destroy(&editBuffer);
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
