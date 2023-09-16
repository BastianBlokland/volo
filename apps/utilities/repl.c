#include "app_cli.h"
#include "core_alloc.h"
#include "core_file.h"
#include "core_file_monitor.h"
#include "core_format.h"
#include "core_path.h"
#include "core_thread.h"
#include "core_time.h"
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
  ReplFlags_Watch        = 1 << 0,
  ReplFlags_OutputTokens = 1 << 1,
  ReplFlags_OutputAst    = 1 << 2,
  ReplFlags_OutputStats  = 1 << 3,
} ReplFlags;

typedef struct {
  u32 exprs[ScriptExprType_Count];
  u32 exprsTotal;
} ReplScriptStats;

static void repl_script_collect_stats(void* ctx, const ScriptDoc* doc, const ScriptExpr expr) {
  ReplScriptStats* stats = ctx;
  ++stats->exprs[script_expr_type(doc, expr)];
  ++stats->exprsTotal;
}

static void repl_output(const String text) { file_write_sync(g_file_stdout, text); }

static void repl_output_result_error(const ScriptReadResult* res) {
  Mem       bufferMem = alloc_alloc(g_alloc_scratch, usize_kibibyte, 1);
  DynString buffer    = dynstring_create_over(bufferMem);

  const TtyStyle styleErr     = ttystyle(.bgColor = TtyBgColor_Red, .flags = TtyStyleFlags_Bold);
  const TtyStyle styleDefault = ttystyle();

  tty_write_style_sequence(&buffer, styleErr);

  fmt_write(
      &buffer,
      "{}:{}-{}:{}: {}",
      fmt_int(res->errorStart.line),
      fmt_int(res->errorStart.column),
      fmt_int(res->errorEnd.line),
      fmt_int(res->errorEnd.column),
      fmt_text(script_error_str(res->error)));

  tty_write_style_sequence(&buffer, styleDefault);
  dynstring_append_char(&buffer, '\n');

  repl_output(dynstring_view(&buffer));
  dynstring_destroy(&buffer);
}

static void repl_output_tokens(String text) {
  Mem       bufferMem = alloc_alloc(g_alloc_scratch, 8 * usize_kibibyte, 1);
  DynString buffer    = dynstring_create_over(bufferMem);

  for (;;) {
    ScriptToken token;
    text = script_lex(text, null, &token, ScriptLexFlags_IncludeComments);
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

static void repl_output_ast(const ScriptDoc* script, const ScriptExpr expr) {
  repl_output(fmt_write_scratch("{}\n", script_expr_fmt(script, expr)));
}

static void repl_output_stats(const ScriptDoc* script, const ScriptExpr expr) {
  ReplScriptStats stats = {0};
  script_expr_visit(script, expr, &stats, repl_script_collect_stats);

  Mem       bufferMem = alloc_alloc(g_alloc_scratch, usize_kibibyte, 1);
  DynString buffer    = dynstring_create_over(bufferMem);

  // clang-format off
  fmt_write(&buffer, "Expr value:     {}\n", fmt_int(stats.exprs[ScriptExprType_Value]));
  fmt_write(&buffer, "Expr var-load:  {}\n", fmt_int(stats.exprs[ScriptExprType_VarLoad]));
  fmt_write(&buffer, "Expr var-store: {}\n", fmt_int(stats.exprs[ScriptExprType_VarStore]));
  fmt_write(&buffer, "Expr mem-load:  {}\n", fmt_int(stats.exprs[ScriptExprType_MemLoad]));
  fmt_write(&buffer, "Expr mem-store: {}\n", fmt_int(stats.exprs[ScriptExprType_MemStore]));
  fmt_write(&buffer, "Expr intrinsic: {}\n", fmt_int(stats.exprs[ScriptExprType_Intrinsic]));
  fmt_write(&buffer, "Expr block:     {}\n", fmt_int(stats.exprs[ScriptExprType_Block]));
  fmt_write(&buffer, "Expr total:     {}\n", fmt_int(stats.exprsTotal));
  fmt_write(&buffer, "Values total:   {}\n", fmt_int(script_values_total(script)));
  // clang-format on

  repl_output(dynstring_view(&buffer));
  dynstring_destroy(&buffer);
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
  case ScriptTokenType_Var:
    return TtyFgColor_Cyan;
  case ScriptTokenType_Comment:
    return TtyFgColor_BrightBlack;
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

static void repl_exec(ScriptMem* mem, const ReplFlags flags, const String input) {
  if (flags & ReplFlags_OutputTokens) {
    repl_output_tokens(input);
  }

  ScriptDoc*       script  = script_create(g_alloc_heap);
  ScriptBinder*    binder  = null;
  void*            bindCtx = null;
  ScriptReadResult res;
  script_read(script, binder, input, &res);

  if (res.type == ScriptResult_Success) {
    if (flags & ReplFlags_OutputAst) {
      repl_output_ast(script, res.expr);
    }
    if (flags & ReplFlags_OutputStats) {
      repl_output_stats(script, res.expr);
    }
    const ScriptVal value = script_eval(script, mem, res.expr, binder, bindCtx);
    repl_output(fmt_write_scratch("{}\n", script_val_fmt(value)));
  } else {
    repl_output_result_error(&res);
  }

  script_destroy(script);
}

typedef struct {
  ReplFlags  flags;
  String     editPrevText;
  DynString* editBuffer;
  ScriptMem* mem;
} ReplEditor;

static bool repl_edit_empty(const ReplEditor* editor) {
  return string_is_empty(dynstring_view(editor->editBuffer));
}

static void repl_edit_prev(const ReplEditor* editor) {
  if (!string_is_empty(editor->editPrevText)) {
    dynstring_clear(editor->editBuffer);
    dynstring_append(editor->editBuffer, editor->editPrevText);
  }
}

static void repl_edit_clear(const ReplEditor* editor) { dynstring_clear(editor->editBuffer); }

static void repl_edit_insert(const ReplEditor* editor, const Unicode cp) {
  utf8_cp_write(editor->editBuffer, cp);
}

static void repl_edit_delete(const ReplEditor* editor) {
  // Delete the last utf8 code-point.
  String str = dynstring_view(editor->editBuffer);
  for (usize i = str.size; i-- > 0;) {
    if (!utf8_contchar(*string_at(str, i))) {
      dynstring_erase_chars(editor->editBuffer, i, str.size - i);
      return;
    }
  }
}

static void repl_edit_submit(ReplEditor* editor) {
  repl_output(string_lit("\n")); // Preserve the input line.

  string_maybe_free(g_alloc_heap, editor->editPrevText);
  editor->editPrevText = string_maybe_dup(g_alloc_heap, dynstring_view(editor->editBuffer));

  repl_exec(editor->mem, editor->flags, dynstring_view(editor->editBuffer));

  dynstring_clear(editor->editBuffer);
}

static void repl_edit_render(const ReplEditor* editor) {
  DynString buffer = dynstring_create(g_alloc_heap, usize_kibibyte);

  tty_write_clear_line_sequence(&buffer, TtyClearMode_All); // Clear line.
  tty_write_set_cursor_hor_sequence(&buffer, 0);            // Move cursor to beginning of line.
  tty_write_line_wrap_sequence(&buffer, false);             // Disable line wrap.

  // Render header.
  tty_write_style_sequence(&buffer, ttystyle(.flags = TtyStyleFlags_Faint));
  dynstring_append(&buffer, string_lit("> "));
  tty_write_style_sequence(&buffer, ttystyle());

  // Render edit text.
  String      editText = dynstring_view(editor->editBuffer);
  ScriptToken token;
  for (;;) {
    const String remText   = script_lex(editText, null, &token, ScriptLexFlags_IncludeComments);
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

static bool repl_edit_update(ReplEditor* editor, TtyInputToken* input) {
  switch (input->type) {
  case TtyInputType_Interrupt:
    return false; // Stop.
  case TtyInputType_KeyEscape:
    repl_edit_clear(editor);
    break;
  case TtyInputType_Text:
    repl_edit_insert(editor, input->val_text);
    break;
  case TtyInputType_KeyBackspace:
    repl_edit_delete(editor);
    break;
  case TtyInputType_KeyUp:
    repl_edit_prev(editor);
    break;
  case TtyInputType_Accept:
    if (!repl_edit_empty(editor)) {
      repl_edit_submit(editor);
    }
    break;
  default:
    break;
  }
  repl_edit_render(editor);
  return true; // Keep running.
}

static i32 repl_run_interactive(const ReplFlags flags) {
  DynString readBuffer = dynstring_create(g_alloc_heap, 32);
  DynString editBuffer = dynstring_create(g_alloc_heap, 128);

  ReplEditor editor = {
      .flags      = flags,
      .editBuffer = &editBuffer,
      .mem        = script_mem_create(g_alloc_heap),
  };

  tty_opts_set(g_file_stdin, TtyOpts_NoEcho | TtyOpts_NoBuffer | TtyOpts_NoSignals);
  repl_edit_render(&editor);

  while (tty_read(g_file_stdin, &readBuffer, TtyReadFlags_None)) {
    String        readStr = dynstring_view(&readBuffer);
    TtyInputToken input;
    for (;;) {
      readStr = tty_input_lex(readStr, &input);
      if (input.type == TtyInputType_End) {
        break;
      }
      if (!repl_edit_update(&editor, &input)) {
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
  string_maybe_free(g_alloc_heap, editor.editPrevText);
  script_mem_destroy(editor.mem);
  return 0;
}

static i32 repl_run_file(File* file, const ReplFlags flags) {
  DynString readBuffer = dynstring_create(g_alloc_heap, 1 * usize_kibibyte);
  file_read_to_end_sync(file, &readBuffer);

  ScriptMem* mem = script_mem_create(g_alloc_heap);
  repl_exec(mem, flags, dynstring_view(&readBuffer));
  script_mem_destroy(mem);

  dynstring_destroy(&readBuffer);
  return 0;
}

static i32 repl_run_path(const String path, const ReplFlags flags) {
  File*      file;
  FileResult fileRes;
  u32        fileLockedRetries = 0;

Retry:
  fileRes = file_create(g_alloc_heap, path, FileMode_Open, FileAccess_Read, &file);
  if (fileRes == FileResult_Locked && fileLockedRetries++ < 10) {
    thread_sleep(time_milliseconds(100));
    goto Retry;
  } else if (fileRes != FileResult_Success) {
    const String err = file_result_str(fileRes);
    const String msg = fmt_write_scratch("ERROR: Failed to open file: {}\n", fmt_text(err));
    file_write_sync(g_file_stderr, msg);
    return 1;
  }
  const i32 runRes = repl_run_file(file, flags);
  file_destroy(file);
  return runRes;
}

static i32 repl_run_watch(const String path, const ReplFlags flags) {
  i32 res = 0;

  // Compute the absolute parent path and the canonized relative path, this is needed to support
  // paths that start with '../'.
  const String pathAbs      = string_dup(g_alloc_heap, path_build_scratch(path));
  const String parentAbs    = path_parent(pathAbs);
  const String pathRelCanon = string_dup(g_alloc_heap, path_canonize_scratch(path));

  const FileMonitorFlags monFlags = FileMonitorFlags_Blocking;
  FileMonitor*           mon      = file_monitor_create(g_alloc_heap, parentAbs, monFlags);

  FileMonitorResult monRes;
  if ((monRes = file_monitor_watch(mon, pathRelCanon, 0))) {
    const String err = file_monitor_result_str(monRes);
    const String msg = fmt_write_scratch("ERROR: Failed to watch path: {}\n", fmt_text(err));
    file_write_sync(g_file_stderr, msg);
    res = 1;
    goto Ret;
  }

  FileMonitorEvent evt;
  do {
    repl_output(string_lit("--- Change detected ---\n"));
    res = repl_run_path(path, flags);
  } while (file_monitor_poll(mon, &evt));

Ret:
  string_free(g_alloc_heap, pathAbs);
  string_free(g_alloc_heap, pathRelCanon);
  file_monitor_destroy(mon);
  return res;
}

static CliId g_fileArg, g_watchFlag, g_tokensFlag, g_astFlag, g_statsFlag, g_helpFlag;

void app_cli_configure(CliApp* app) {
  static const String g_desc = string_static("Execute a script from a file or stdin "
                                             "(interactive when stdin is a tty).");
  cli_app_register_desc(app, g_desc);

  g_fileArg = cli_register_arg(app, string_lit("file"), CliOptionFlags_Value);
  cli_register_desc(app, g_fileArg, string_lit("File to execute (default: stdin)."));
  cli_register_validator(app, g_fileArg, cli_validate_file_regular);

  g_watchFlag = cli_register_flag(app, 'w', string_lit("watch"), CliOptionFlags_None);
  cli_register_desc(app, g_watchFlag, string_lit("Reevaluate the script when the file changes."));

  g_tokensFlag = cli_register_flag(app, 't', string_lit("tokens"), CliOptionFlags_None);
  cli_register_desc(app, g_tokensFlag, string_lit("Ouput the tokens."));

  g_astFlag = cli_register_flag(app, 'a', string_lit("ast"), CliOptionFlags_None);
  cli_register_desc(app, g_astFlag, string_lit("Ouput the abstract-syntax-tree expressions."));

  g_statsFlag = cli_register_flag(app, 's', string_lit("stats"), CliOptionFlags_None);
  cli_register_desc(app, g_statsFlag, string_lit("Ouput script statistics."));

  g_helpFlag = cli_register_flag(app, 'h', string_lit("help"), CliOptionFlags_None);
  cli_register_desc(app, g_helpFlag, string_lit("Display this help page."));
  cli_register_exclusions(app, g_helpFlag, g_fileArg);
  cli_register_exclusions(app, g_helpFlag, g_watchFlag);
  cli_register_exclusions(app, g_helpFlag, g_tokensFlag);
  cli_register_exclusions(app, g_helpFlag, g_astFlag);
  cli_register_exclusions(app, g_helpFlag, g_statsFlag);
}

i32 app_cli_run(const CliApp* app, const CliInvocation* invoc) {
  if (cli_parse_provided(invoc, g_helpFlag)) {
    cli_help_write_file(app, g_file_stdout);
    return 0;
  }

  ReplFlags flags = ReplFlags_None;
  if (cli_parse_provided(invoc, g_watchFlag)) {
    flags |= ReplFlags_Watch;
  }
  if (cli_parse_provided(invoc, g_tokensFlag)) {
    flags |= ReplFlags_OutputTokens;
  }
  if (cli_parse_provided(invoc, g_astFlag)) {
    flags |= ReplFlags_OutputAst;
  }
  if (cli_parse_provided(invoc, g_statsFlag)) {
    flags |= ReplFlags_OutputStats;
  }

  if (!tty_isatty(g_file_stdout)) {
    // TODO: Support non-tty output for non-interactive modes by conditionally removing the styling.
    file_write_sync(g_file_stderr, string_lit("ERROR: REPL needs a tty output stream.\n"));
    return 1;
  }

  const CliParseValues fileArg = cli_parse_values(invoc, g_fileArg);
  if (fileArg.count) {
    if (flags & ReplFlags_Watch) {
      return repl_run_watch(fileArg.values[0], flags);
    }
    return repl_run_path(fileArg.values[0], flags);
  }
  if (tty_isatty(g_file_stdin)) {
    return repl_run_interactive(flags);
  }
  return repl_run_file(g_file_stdin, flags);
}
