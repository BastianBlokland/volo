#include "app_cli.h"
#include "core_alloc.h"
#include "core_file.h"
#include "core_format.h"
#include "core_tty.h"
#include "core_utf8.h"
#include "script_eval.h"
#include "script_lex.h"
#include "script_mem.h"
#include "script_read.h"

/**
 * ReadEvalPrintLoop - Utility to play around with script execution.
 */

typedef enum {
  ReplFlags_None         = 0,
  ReplFlags_OutputTokens = 1 << 0,
  ReplFlags_OutputAst    = 1 << 1,
} ReplFlags;

static void repl_output(const String text) { file_write_sync(g_file_stdout, text); }

static void repl_output_error(const String message) {
  const String text = fmt_write_scratch(
      "{}ERROR: {}{}",
      fmt_ttystyle(.bgColor = TtyBgColor_Red, .flags = TtyStyleFlags_Bold),
      fmt_text(message),
      fmt_ttystyle());
  repl_output(text);
}

static void repl_output_debug_tokens(String text) {
  Mem       bufferMem = alloc_alloc(g_alloc_scratch, usize_kibibyte, 1);
  DynString buffer    = dynstring_create_over(bufferMem);

  for (;;) {
    ScriptToken token;
    text = script_lex(text, null, &token);
    if (token.type == ScriptTokenType_End) {
      break;
    }
    dynstring_append(&buffer, script_token_str_scratch(&token));
    dynstring_append_char(&buffer, ' ');
  }
  dynstring_append_char(&buffer, '\n');

  repl_output(dynstring_view(&buffer));
  dynstring_destroy(&buffer);
}

static void repl_output_debug_ast(const ScriptDoc* script, const ScriptExpr expr) {
  repl_output(fmt_write_scratch("{}\n", script_expr_fmt(script, expr)));
}

static TtyFgColor repl_token_color(const ScriptTokenType tokenType) {
  switch (tokenType) {
  case ScriptTokenType_Error:
    return TtyFgColor_BrightRed;
  case ScriptTokenType_Number:
  case ScriptTokenType_String:
    return TtyFgColor_Yellow;
  case ScriptTokenType_Identifier:
    return TtyFgColor_Magenta;
  case ScriptTokenType_Key:
    return TtyFgColor_Blue;
  case ScriptTokenType_Eq:
  case ScriptTokenType_EqEq:
  case ScriptTokenType_Bang:
  case ScriptTokenType_BangEq:
  case ScriptTokenType_Le:
  case ScriptTokenType_LeEq:
  case ScriptTokenType_Gt:
  case ScriptTokenType_GtEq:
  case ScriptTokenType_Plus:
  case ScriptTokenType_PlusEq:
  case ScriptTokenType_Minus:
  case ScriptTokenType_MinusEq:
  case ScriptTokenType_Star:
  case ScriptTokenType_StarEq:
  case ScriptTokenType_Slash:
  case ScriptTokenType_SlashEq:
  case ScriptTokenType_Percent:
  case ScriptTokenType_PercentEq:
  case ScriptTokenType_Colon:
  case ScriptTokenType_SemiColon:
  case ScriptTokenType_AmpAmp:
  case ScriptTokenType_PipePipe:
  case ScriptTokenType_QMark:
  case ScriptTokenType_QMarkQMark:
  case ScriptTokenType_QMarkQMarkEq:
    return TtyFgColor_Green;
  case ScriptTokenType_If:
  case ScriptTokenType_Else:
    return TtyFgColor_Cyan;
  case ScriptTokenType_ParenOpen:
  case ScriptTokenType_ParenClose:
  case ScriptTokenType_CurlyOpen:
  case ScriptTokenType_CurlyClose:
  case ScriptTokenType_Comma:
  case ScriptTokenType_End:
    break;
  }
  return TtyFgColor_Default;
}

typedef struct {
  ReplFlags  flags;
  String     editPrevText;
  DynString* editBuffer;
  ScriptMem* scriptMem;
} ReplState;

static bool repl_edit_empty(const ReplState* state) {
  return string_is_empty(dynstring_view(state->editBuffer));
}

static void repl_edit_prev(const ReplState* state) {
  if (!string_is_empty(state->editPrevText)) {
    dynstring_clear(state->editBuffer);
    dynstring_append(state->editBuffer, state->editPrevText);
  }
}

static void repl_edit_clear(const ReplState* state) { dynstring_clear(state->editBuffer); }

static void repl_edit_insert(const ReplState* state, const Unicode cp) {
  utf8_cp_write(state->editBuffer, cp);
}

static void repl_edit_delete(const ReplState* state) {
  // Delete the last utf8 code-point.
  String str = dynstring_view(state->editBuffer);
  for (usize i = str.size; i-- > 0;) {
    if (!utf8_contchar(*string_at(str, i))) {
      dynstring_erase_chars(state->editBuffer, i, str.size - i);
      return;
    }
  }
}

static void repl_edit_submit(ReplState* state) {
  repl_output(string_lit("\n")); // Preserve the input line.

  string_maybe_free(g_alloc_heap, state->editPrevText);
  state->editPrevText = string_maybe_dup(g_alloc_heap, dynstring_view(state->editBuffer));

  if (state->flags & ReplFlags_OutputTokens) {
    repl_output_debug_tokens(dynstring_view(state->editBuffer));
  }

  ScriptDoc*       script = script_create(g_alloc_heap);
  ScriptReadResult res;
  script_read(script, dynstring_view(state->editBuffer), &res);

  if (res.type == ScriptResult_Success) {
    if (state->flags & ReplFlags_OutputAst) {
      repl_output_debug_ast(script, res.expr);
    }
    const ScriptVal value = script_eval(script, state->scriptMem, res.expr);
    repl_output(fmt_write_scratch("{}\n", script_val_fmt(value)));
  } else {
    repl_output_error(fmt_write_scratch("{}\n", fmt_text(script_error_str(res.error))));
  }

  script_destroy(script);
  dynstring_clear(state->editBuffer);
}

static void repl_edit_render(const ReplState* state) {
  Mem       bufferMem = alloc_alloc(g_alloc_scratch, usize_kibibyte, 1);
  DynString buffer    = dynstring_create_over(bufferMem);

  tty_write_clear_line_sequence(&buffer, TtyClearMode_All); // Clear line.
  tty_write_set_cursor_hor_sequence(&buffer, 0);            // Move cursor to beginning of line.
  tty_write_line_wrap_sequence(&buffer, false);             // Disable line wrap.

  // Render header.
  tty_write_style_sequence(&buffer, ttystyle(.flags = TtyStyleFlags_Faint));
  dynstring_append(&buffer, string_lit("> "));
  tty_write_style_sequence(&buffer, ttystyle());

  // Render edit text.
  String      editText = dynstring_view(state->editBuffer);
  ScriptToken token;
  for (;;) {
    const String remText   = script_lex(editText, null, &token);
    const usize  tokenSize = editText.size - remText.size;
    const String tokenText = string_slice(editText, 0, tokenSize);
    tty_write_style_sequence(&buffer, ttystyle(.fgColor = repl_token_color(token.type)));
    dynstring_append(&buffer, tokenText);
    if (token.type == ScriptTokenType_End) {
      break;
    }
    editText = remText;
  }

  tty_write_style_sequence(&buffer, ttystyle());
  repl_output(dynstring_view(&buffer));
  dynstring_destroy(&buffer);
}

static void repl_edit_render_cleanup() {
  Mem       bufferMem = alloc_alloc(g_alloc_scratch, usize_kibibyte, 1);
  DynString buffer    = dynstring_create_over(bufferMem);

  tty_write_clear_line_sequence(&buffer, TtyClearMode_All);
  tty_write_set_cursor_hor_sequence(&buffer, 0);
  tty_write_line_wrap_sequence(&buffer, true); // TODO: Only do this if it was originally enabled?

  repl_output(dynstring_view(&buffer));
  dynstring_destroy(&buffer);
}

static bool repl_update(ReplState* state, TtyInputToken* input) {
  switch (input->type) {
  case TtyInputType_Interrupt:
    return false; // Stop.
  case TtyInputType_KeyEscape:
    repl_edit_clear(state);
    break;
  case TtyInputType_Text:
    repl_edit_insert(state, input->val_text);
    break;
  case TtyInputType_KeyBackspace:
    repl_edit_delete(state);
    break;
  case TtyInputType_KeyUp:
    repl_edit_prev(state);
    break;
  case TtyInputType_Accept:
    if (!repl_edit_empty(state)) {
      repl_edit_submit(state);
    }
    break;
  default:
    break;
  }
  repl_edit_render(state);
  return true; // Keep running.
}

static i32 repl_run_interactive(const ReplFlags flags) {
  if (!tty_isatty(g_file_stdin) || !tty_isatty(g_file_stdout)) {
    file_write_sync(g_file_stderr, string_lit("ERROR: REPL has to be ran interactively\n"));
    return 1;
  }

  DynString readBuffer = dynstring_create(g_alloc_heap, 32);
  DynString editBuffer = dynstring_create(g_alloc_heap, 128);

  ReplState state = {
      .flags      = flags,
      .editBuffer = &editBuffer,
      .scriptMem  = script_mem_create(g_alloc_heap),
  };

  tty_opts_set(g_file_stdin, TtyOpts_NoEcho | TtyOpts_NoBuffer | TtyOpts_NoSignals);
  repl_edit_render(&state);

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
  repl_edit_render_cleanup();
  tty_opts_set(g_file_stdin, TtyOpts_None);

  dynstring_destroy(&readBuffer);
  dynstring_destroy(&editBuffer);
  string_maybe_free(g_alloc_heap, state.editPrevText);
  script_mem_destroy(state.scriptMem);
  return 0;
}

static CliId g_tokensFlag, g_astFlag, g_helpFlag;

void app_cli_configure(CliApp* app) {
  cli_app_register_desc(app, string_lit("Script ReadEvalPrintLoop utility."));

  g_tokensFlag = cli_register_flag(app, 't', string_lit("tokens"), CliOptionFlags_None);
  cli_register_desc(app, g_tokensFlag, string_lit("Ouput the tokens."));

  g_astFlag = cli_register_flag(app, 'a', string_lit("ast"), CliOptionFlags_None);
  cli_register_desc(app, g_astFlag, string_lit("Ouput the abstract-syntax-tree expressions."));

  g_helpFlag = cli_register_flag(app, 'h', string_lit("help"), CliOptionFlags_None);
  cli_register_desc(app, g_helpFlag, string_lit("Display this help page."));
  cli_register_exclusions(app, g_helpFlag, g_tokensFlag);
  cli_register_exclusions(app, g_helpFlag, g_astFlag);
}

i32 app_cli_run(const CliApp* app, const CliInvocation* invoc) {
  if (cli_parse_provided(invoc, g_helpFlag)) {
    cli_help_write_file(app, g_file_stdout);
    return 0;
  }

  ReplFlags flags = ReplFlags_None;
  if (cli_parse_provided(invoc, g_tokensFlag)) {
    flags |= ReplFlags_OutputTokens;
  }
  if (cli_parse_provided(invoc, g_astFlag)) {
    flags |= ReplFlags_OutputAst;
  }

  return repl_run_interactive(flags);
}
