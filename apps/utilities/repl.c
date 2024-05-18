#include "app_cli.h"
#include "core_alloc.h"
#include "core_diag.h"
#include "core_file.h"
#include "core_file_monitor.h"
#include "core_format.h"
#include "core_path.h"
#include "core_thread.h"
#include "core_time.h"
#include "core_tty.h"
#include "core_utf8.h"
#include "script_binder.h"
#include "script_diag.h"
#include "script_eval.h"
#include "script_lex.h"
#include "script_mem.h"
#include "script_read.h"
#include "script_sym.h"

/**
 * ReadEvalPrintLoop - Utility to play around with script execution.
 */

typedef enum {
  ReplFlags_None          = 0,
  ReplFlags_NoEval        = 1 << 0,
  ReplFlags_Watch         = 1 << 1,
  ReplFlags_OutputTokens  = 1 << 2,
  ReplFlags_OutputAst     = 1 << 3,
  ReplFlags_OutputStats   = 1 << 4,
  ReplFlags_OutputSymbols = 1 << 5,
} ReplFlags;

typedef struct {
  u32 exprs[ScriptExprKind_Count];
  u32 exprsTotal;
} ReplScriptStats;

static void repl_script_collect_stats(void* ctx, const ScriptDoc* doc, const ScriptExpr expr) {
  ReplScriptStats* stats = ctx;
  ++stats->exprs[script_expr_kind(doc, expr)];
  ++stats->exprsTotal;
}

static void repl_output(const String text) { file_write_sync(g_fileStdOut, text); }

static void repl_output_diag(const String src, const ScriptDiag* diag, const String id) {
  Mem       bufferMem = alloc_alloc(g_allocScratch, usize_kibibyte, 1);
  DynString buffer    = dynstring_create_over(bufferMem);

  const TtyStyle styleErr     = ttystyle(.bgColor = TtyBgColor_Red, .flags = TtyStyleFlags_Bold);
  const TtyStyle styleWarn    = ttystyle(.bgColor = TtyBgColor_Yellow, .flags = TtyStyleFlags_Bold);
  const TtyStyle styleDefault = ttystyle();

  switch (diag->severity) {
  case ScriptDiagSeverity_Error:
    tty_write_style_sequence(&buffer, styleErr);
    break;
  case ScriptDiagSeverity_Warning:
    tty_write_style_sequence(&buffer, styleWarn);
    break;
  }

  if (!string_is_empty(id)) {
    dynstring_append(&buffer, id);
    dynstring_append_char(&buffer, ':');
  }
  script_diag_pretty_write(&buffer, src, diag);

  tty_write_style_sequence(&buffer, styleDefault);
  dynstring_append_char(&buffer, '\n');

  repl_output(dynstring_view(&buffer));
  dynstring_destroy(&buffer);
}

static void repl_output_panic(const String src, const ScriptPanic* panic, const String id) {
  Mem       bufferMem = alloc_alloc(g_allocScratch, usize_kibibyte, 1);
  DynString buffer    = dynstring_create_over(bufferMem);

  const TtyStyle styleErr     = ttystyle(.bgColor = TtyBgColor_Red, .flags = TtyStyleFlags_Bold);
  const TtyStyle styleDefault = ttystyle();

  tty_write_style_sequence(&buffer, styleErr);

  if (!string_is_empty(id)) {
    dynstring_append(&buffer, id);
    dynstring_append(&buffer, string_lit(": "));
  }
  script_panic_pretty_write(&buffer, src, panic);

  tty_write_style_sequence(&buffer, styleDefault);
  dynstring_append_char(&buffer, '\n');

  repl_output(dynstring_view(&buffer));
  dynstring_destroy(&buffer);
}

static void repl_output_sym(const ScriptSymBag* symBag, const ScriptSym sym) {
  Mem       bufferMem = alloc_alloc(g_allocScratch, usize_kibibyte, 1);
  DynString buffer    = dynstring_create_over(bufferMem);

  dynstring_append(&buffer, string_lit("Sym: "));
  script_sym_write(&buffer, symBag, sym);
  dynstring_append_char(&buffer, '\n');

  repl_output(dynstring_view(&buffer));
  dynstring_destroy(&buffer);
}

static void repl_output_tokens(String text) {
  Mem       bufferMem = alloc_alloc(g_allocScratch, 8 * usize_kibibyte, 1);
  DynString buffer    = dynstring_create_over(bufferMem);

  dynstring_append(&buffer, string_lit("Tokens: "));

  const ScriptLexFlags flags = ScriptLexFlags_IncludeComments | ScriptLexFlags_IncludeNewlines;

  for (;;) {
    ScriptToken token;
    text = script_lex(text, null, &token, flags);
    if (token.kind == ScriptTokenKind_End) {
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

  Mem       bufferMem = alloc_alloc(g_allocScratch, usize_kibibyte, 1);
  DynString buffer    = dynstring_create_over(bufferMem);

  // clang-format off
  fmt_write(&buffer, "Expr value:     {}\n", fmt_int(stats.exprs[ScriptExprKind_Value]));
  fmt_write(&buffer, "Expr var-load:  {}\n", fmt_int(stats.exprs[ScriptExprKind_VarLoad]));
  fmt_write(&buffer, "Expr var-store: {}\n", fmt_int(stats.exprs[ScriptExprKind_VarStore]));
  fmt_write(&buffer, "Expr mem-load:  {}\n", fmt_int(stats.exprs[ScriptExprKind_MemLoad]));
  fmt_write(&buffer, "Expr mem-store: {}\n", fmt_int(stats.exprs[ScriptExprKind_MemStore]));
  fmt_write(&buffer, "Expr intrinsic: {}\n", fmt_int(stats.exprs[ScriptExprKind_Intrinsic]));
  fmt_write(&buffer, "Expr block:     {}\n", fmt_int(stats.exprs[ScriptExprKind_Block]));
  fmt_write(&buffer, "Expr extern:    {}\n", fmt_int(stats.exprs[ScriptExprKind_Extern]));
  fmt_write(&buffer, "Expr total:     {}\n", fmt_int(stats.exprsTotal));
  fmt_write(&buffer, "Values total:   {}\n", fmt_int(script_values_total(script)));
  // clang-format on

  repl_output(dynstring_view(&buffer));
  dynstring_destroy(&buffer);
}

static TtyFgColor repl_token_color(const ScriptTokenKind tokenKind) {
  switch (tokenKind) {
  case ScriptTokenKind_Diag:
    return TtyFgColor_BrightRed;
  case ScriptTokenKind_Number:
  case ScriptTokenKind_String:
    return TtyFgColor_Yellow;
  case ScriptTokenKind_Identifier:
    return TtyFgColor_Magenta;
  case ScriptTokenKind_Key:
    return TtyFgColor_Blue;
  case ScriptTokenKind_Eq:
  case ScriptTokenKind_EqEq:
  case ScriptTokenKind_Bang:
  case ScriptTokenKind_BangEq:
  case ScriptTokenKind_Le:
  case ScriptTokenKind_LeEq:
  case ScriptTokenKind_Gt:
  case ScriptTokenKind_GtEq:
  case ScriptTokenKind_Plus:
  case ScriptTokenKind_PlusEq:
  case ScriptTokenKind_Minus:
  case ScriptTokenKind_MinusEq:
  case ScriptTokenKind_Star:
  case ScriptTokenKind_StarEq:
  case ScriptTokenKind_Slash:
  case ScriptTokenKind_SlashEq:
  case ScriptTokenKind_Percent:
  case ScriptTokenKind_PercentEq:
  case ScriptTokenKind_Colon:
  case ScriptTokenKind_Semicolon:
  case ScriptTokenKind_AmpAmp:
  case ScriptTokenKind_PipePipe:
  case ScriptTokenKind_QMark:
  case ScriptTokenKind_QMarkQMark:
  case ScriptTokenKind_QMarkQMarkEq:
    return TtyFgColor_Green;
  case ScriptTokenKind_If:
  case ScriptTokenKind_Else:
  case ScriptTokenKind_Var:
  case ScriptTokenKind_While:
  case ScriptTokenKind_For:
  case ScriptTokenKind_Continue:
  case ScriptTokenKind_Break:
  case ScriptTokenKind_Return:
    return TtyFgColor_Cyan;
  case ScriptTokenKind_CommentLine:
  case ScriptTokenKind_CommentBlock:
    return TtyFgColor_BrightBlack;
  case ScriptTokenKind_ParenOpen:
  case ScriptTokenKind_ParenClose:
  case ScriptTokenKind_CurlyOpen:
  case ScriptTokenKind_CurlyClose:
  case ScriptTokenKind_Comma:
  case ScriptTokenKind_Newline:
  case ScriptTokenKind_End:
    break;
  }
  return TtyFgColor_Default;
}

static ScriptVal repl_bind_print(void* ctx, const ScriptArgs args, ScriptError* err) {
  (void)ctx;
  (void)err;

  Mem       bufferMem = alloc_alloc(g_allocScratch, usize_kibibyte, 1);
  DynString buffer    = dynstring_create_over(bufferMem);

  for (usize i = 0; i != args.count; ++i) {
    if (i) {
      dynstring_append_char(&buffer, ' ');
    }
    script_val_write(args.values[i], &buffer);
  }
  dynstring_append_char(&buffer, '\n');

  repl_output(dynstring_view(&buffer));
  dynstring_destroy(&buffer);

  return script_null();
}

static ScriptVal repl_bind_print_bytes(void* ctx, const ScriptArgs args, ScriptError* err) {
  (void)ctx;
  (void)err;

  Mem       bufferMem = alloc_alloc(g_allocScratch, usize_kibibyte, 1);
  DynString buffer    = dynstring_create_over(bufferMem);

  for (usize i = 0; i != args.count; ++i) {
    format_write_mem(&buffer, mem_var(args.values[i]));
    dynstring_append_char(&buffer, '\n');
  }

  repl_output(dynstring_view(&buffer));
  dynstring_destroy(&buffer);

  return script_null();
}

static ScriptVal repl_bind_print_bits(void* ctx, const ScriptArgs args, ScriptError* err) {
  (void)ctx;
  (void)err;

  Mem       bufferMem = alloc_alloc(g_allocScratch, usize_kibibyte, 1);
  DynString buffer    = dynstring_create_over(bufferMem);

  for (usize i = 0; i != args.count; ++i) {
    format_write_bitset(&buffer, bitset_from_var(args.values[i]));
    dynstring_append_char(&buffer, '\n');
  }

  repl_output(dynstring_view(&buffer));
  dynstring_destroy(&buffer);

  return script_null();
}

static void repl_bind_init(ScriptBinder* binder) {
  const String     doc = string_empty;
  const ScriptSig* sig = null;

  script_binder_declare(binder, string_lit("print"), doc, sig, &repl_bind_print);
  script_binder_declare(binder, string_lit("print_bytes"), doc, sig, &repl_bind_print_bytes);
  script_binder_declare(binder, string_lit("print_bits"), doc, sig, &repl_bind_print_bits);
}

static void repl_exec(
    const ScriptBinder* binder,
    ScriptMem*          mem,
    const ReplFlags     flags,
    const String        input,
    const String        id) {
  if (flags & ReplFlags_OutputTokens) {
    repl_output_tokens(input);
  }

  Allocator* tempAlloc = alloc_bump_create_stack(2 * usize_kibibyte);

  ScriptDoc*     script = script_create(g_allocHeap);
  ScriptDiagBag* diags  = script_diag_bag_create(tempAlloc, ScriptDiagFilter_All);
  ScriptSymBag*  syms = (flags & ReplFlags_OutputSymbols) ? script_sym_bag_create(g_allocHeap) : 0;

  const ScriptExpr expr = script_read(script, binder, input, diags, syms);

  const u32 diagCount = script_diag_count(diags, ScriptDiagFilter_All);
  for (u32 i = 0; i != diagCount; ++i) {
    repl_output_diag(input, script_diag_data(diags) + i, id);
  }
  if (flags & ReplFlags_OutputSymbols) {
    ScriptSym itr = script_sym_first(syms, script_pos_sentinel);
    for (; !sentinel_check(itr); itr = script_sym_next(syms, script_pos_sentinel, itr)) {
      repl_output_sym(syms, itr);
    }
  }

  if (!sentinel_check(expr)) {
    if (flags & ReplFlags_OutputAst) {
      repl_output_ast(script, expr);
    }
    if (flags & ReplFlags_OutputStats) {
      repl_output_stats(script, expr);
    }
    const bool noErrors = script_diag_count(diags, ScriptDiagFilter_Error) == 0;
    if (noErrors && !(flags & ReplFlags_NoEval)) {
      const ScriptEvalResult evalRes = script_eval(script, mem, expr, binder, null);
      if (script_panic_valid(&evalRes.panic)) {
        repl_output_panic(input, &evalRes.panic, id);
      } else {
        repl_output(fmt_write_scratch("{}\n", script_val_fmt(evalRes.val)));
      }
    }
  }

  script_destroy(script);
  script_diag_bag_destroy(diags);
  if (syms) {
    script_sym_bag_destroy(syms);
  }
}

typedef struct {
  const ScriptBinder* binder;
  ReplFlags           flags;
  String              editPrevText;
  DynString*          editBuffer;
  ScriptMem           mem;
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
  utf8_cp_write_to(editor->editBuffer, cp);
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

  string_maybe_free(g_allocHeap, editor->editPrevText);
  editor->editPrevText = string_maybe_dup(g_allocHeap, dynstring_view(editor->editBuffer));

  const String id = string_empty;
  repl_exec(editor->binder, &editor->mem, editor->flags, dynstring_view(editor->editBuffer), id);

  dynstring_clear(editor->editBuffer);
}

static void repl_edit_render(const ReplEditor* editor) {
  DynString buffer = dynstring_create(g_allocHeap, usize_kibibyte);

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
    tty_write_style_sequence(&buffer, ttystyle(.fgColor = repl_token_color(token.kind)));
    dynstring_append(&buffer, tokenText);
    if (token.kind == ScriptTokenKind_End) {
      break;
    }
    editText = remText;
  }

  tty_write_style_sequence(&buffer, ttystyle());
  repl_output(dynstring_view(&buffer));
  dynstring_destroy(&buffer);
}

static void repl_edit_render_cleanup(void) {
  Mem       bufferMem = alloc_alloc(g_allocScratch, usize_kibibyte, 1);
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

static i32 repl_run_interactive(const ScriptBinder* binder, const ReplFlags flags) {
  DynString readBuffer = dynstring_create(g_allocHeap, 32);
  DynString editBuffer = dynstring_create(g_allocHeap, 128);

  ReplEditor editor = {
      .binder     = binder,
      .flags      = flags,
      .editBuffer = &editBuffer,
      .mem        = script_mem_create(g_allocHeap),
  };

  tty_opts_set(g_fileStdIn, TtyOpts_NoEcho | TtyOpts_NoBuffer | TtyOpts_NoSignals);
  repl_edit_render(&editor);

  while (tty_read(g_fileStdIn, &readBuffer, TtyReadFlags_None)) {
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
  tty_opts_set(g_fileStdIn, TtyOpts_None);

  dynstring_destroy(&readBuffer);
  dynstring_destroy(&editBuffer);
  string_maybe_free(g_allocHeap, editor.editPrevText);
  script_mem_destroy(&editor.mem);
  return 0;
}

static i32
repl_run_file(const ScriptBinder* binder, File* file, const String id, const ReplFlags flags) {
  DynString readBuffer = dynstring_create(g_allocHeap, 1 * usize_kibibyte);
  file_read_to_end_sync(file, &readBuffer);

  ScriptMem mem = script_mem_create(g_allocHeap);
  repl_exec(binder, &mem, flags, dynstring_view(&readBuffer), id);
  script_mem_destroy(&mem);

  dynstring_destroy(&readBuffer);
  return 0;
}

static i32 repl_run_path(const ScriptBinder* binder, const String pathAbs, const ReplFlags flags) {
  diag_assert(path_is_absolute(pathAbs));

  File*      file;
  FileResult fileRes;
  u32        fileLockedRetries = 0;

Retry:
  fileRes = file_create(g_allocHeap, pathAbs, FileMode_Open, FileAccess_Read, &file);
  if (fileRes == FileResult_Locked && fileLockedRetries++ < 10) {
    thread_sleep(time_milliseconds(100));
    goto Retry;
  } else if (fileRes != FileResult_Success) {
    const String err = file_result_str(fileRes);
    const String msg = fmt_write_scratch("ERROR: Failed to open file: {}\n", fmt_text(err));
    file_write_sync(g_fileStdErr, msg);
    return 1;
  }

  const String id     = pathAbs;
  const i32    runRes = repl_run_file(binder, file, id, flags);
  file_destroy(file);
  return runRes;
}

static i32 repl_run_watch(const ScriptBinder* binder, const String pathAbs, const ReplFlags flags) {
  diag_assert(path_is_absolute(pathAbs));
  i32 res = 0;

  const FileMonitorFlags monFlags = FileMonitorFlags_Blocking;
  FileMonitor*           mon = file_monitor_create(g_allocHeap, path_parent(pathAbs), monFlags);

  FileMonitorResult monRes;
  if ((monRes = file_monitor_watch(mon, path_filename(pathAbs), 0))) {
    const String err = file_monitor_result_str(monRes);
    const String msg = fmt_write_scratch("ERROR: Failed to watch path: {}\n", fmt_text(err));
    file_write_sync(g_fileStdErr, msg);
    res = 1;
    goto Ret;
  }

  FileMonitorEvent evt;
  do {
    res = repl_run_path(binder, pathAbs, flags);
    repl_output(string_lit("--- Waiting for change ---\n"));
  } while (file_monitor_poll(mon, &evt));

Ret:
  file_monitor_destroy(mon);
  return res;
}

static bool repl_read_binder_file(ScriptBinder* binder, const String path) {
  bool       success = true;
  File*      file;
  FileResult fileRes;
  if ((fileRes = file_create(g_allocHeap, path, FileMode_Open, FileAccess_Read, &file))) {
    file_write_sync(g_fileStdErr, string_lit("ERROR: Failed to open binder file.\n"));
    success = false;
    goto Ret;
  }
  String fileData;
  if ((fileRes = file_map(file, &fileData))) {
    file_write_sync(g_fileStdErr, string_lit("ERROR: Failed to map binder file.\n"));
    success = false;
    goto Ret;
  }
  if (!script_binder_read(binder, fileData)) {
    file_write_sync(g_fileStdErr, string_lit("ERROR: Invalid binder file.\n"));
    success = false;
    goto Ret;
  }
Ret:
  if (file) {
    file_destroy(file);
  }
  return success;
}

static CliId g_optFile;
static CliId g_optBinder;
static CliId g_optNoEval, g_optWatch, g_optTokens, g_optAst, g_optStats, g_optSyms;
static CliId g_optHelp;

void app_cli_configure(CliApp* app) {
  static const String g_desc = string_static("Execute a script from a file or stdin "
                                             "(interactive when stdin is a tty).");
  cli_app_register_desc(app, g_desc);

  g_optFile = cli_register_arg(app, string_lit("file"), CliOptionFlags_Value);
  cli_register_desc(app, g_optFile, string_lit("File to execute (default: stdin)."));
  cli_register_validator(app, g_optFile, cli_validate_file_regular);

  g_optBinder = cli_register_flag(app, 'b', string_lit("binder"), CliOptionFlags_Value);
  cli_register_desc(app, g_optBinder, string_lit("Script binder schema to use."));
  cli_register_validator(app, g_optBinder, cli_validate_file_regular);

  g_optNoEval = cli_register_flag(app, 'n', string_lit("no-eval"), CliOptionFlags_None);
  cli_register_desc(app, g_optNoEval, string_lit("Skip evaluating the input."));

  g_optWatch = cli_register_flag(app, 'w', string_lit("watch"), CliOptionFlags_None);
  cli_register_desc(app, g_optWatch, string_lit("Reevaluate the script when the file changes."));

  g_optTokens = cli_register_flag(app, 't', string_lit("tokens"), CliOptionFlags_None);
  cli_register_desc(app, g_optTokens, string_lit("Ouput the tokens."));

  g_optAst = cli_register_flag(app, 'a', string_lit("ast"), CliOptionFlags_None);
  cli_register_desc(app, g_optAst, string_lit("Ouput the abstract-syntax-tree expressions."));

  g_optStats = cli_register_flag(app, 's', string_lit("stats"), CliOptionFlags_None);
  cli_register_desc(app, g_optStats, string_lit("Ouput script statistics."));

  g_optSyms = cli_register_flag(app, 'y', string_lit("syms"), CliOptionFlags_None);
  cli_register_desc(app, g_optSyms, string_lit("Ouput script symbols."));

  g_optHelp = cli_register_flag(app, 'h', string_lit("help"), CliOptionFlags_None);
  cli_register_desc(app, g_optHelp, string_lit("Display this help page."));
  cli_register_exclusions(app, g_optHelp, g_optFile);
  cli_register_exclusions(app, g_optHelp, g_optNoEval);
  cli_register_exclusions(app, g_optHelp, g_optWatch);
  cli_register_exclusions(app, g_optHelp, g_optTokens);
  cli_register_exclusions(app, g_optHelp, g_optAst);
  cli_register_exclusions(app, g_optHelp, g_optStats);
  cli_register_exclusions(app, g_optHelp, g_optSyms);
  cli_register_exclusions(app, g_optHelp, g_optBinder);
}

i32 app_cli_run(const CliApp* app, const CliInvocation* invoc) {
  i32           exitCode = 0;
  ScriptBinder* binder   = null;

  if (cli_parse_provided(invoc, g_optHelp)) {
    cli_help_write_file(app, g_fileStdOut);
    goto Exit;
  }

  ReplFlags flags = ReplFlags_None;
  if (cli_parse_provided(invoc, g_optNoEval)) {
    flags |= ReplFlags_NoEval;
  }
  if (cli_parse_provided(invoc, g_optWatch)) {
    flags |= ReplFlags_Watch;
  }
  if (cli_parse_provided(invoc, g_optTokens)) {
    flags |= ReplFlags_OutputTokens;
  }
  if (cli_parse_provided(invoc, g_optAst)) {
    flags |= ReplFlags_OutputAst;
  }
  if (cli_parse_provided(invoc, g_optStats)) {
    flags |= ReplFlags_OutputStats;
  }
  if (cli_parse_provided(invoc, g_optSyms)) {
    flags |= ReplFlags_OutputSymbols;
  }

  if (!tty_isatty(g_fileStdOut)) {
    // TODO: Support non-tty output for non-interactive modes by conditionally removing the styling.
    file_write_sync(g_fileStdErr, string_lit("ERROR: REPL needs a tty output stream.\n"));
    exitCode = 1;
    goto Exit;
  }

  binder = script_binder_create(g_allocHeap);
  repl_bind_init(binder);
  const CliParseValues binderArg = cli_parse_values(invoc, g_optBinder);
  if (binderArg.count) {
    if (!repl_read_binder_file(binder, binderArg.values[0])) {
      exitCode = 1;
      goto Exit;
    }
  }
  script_binder_finalize(binder);

  const CliParseValues fileArg = cli_parse_values(invoc, g_optFile);
  if (fileArg.count) {
    const String pathAbs = string_dup(g_allocPersist, path_build_scratch(fileArg.values[0]));
    if (flags & ReplFlags_Watch) {
      exitCode = repl_run_watch(binder, pathAbs, flags);
    } else {
      exitCode = repl_run_path(binder, pathAbs, flags);
    }
  } else if (tty_isatty(g_fileStdIn)) {
    exitCode = repl_run_interactive(binder, flags);
  } else {
    const String id = string_empty;
    exitCode        = repl_run_file(binder, g_fileStdIn, id, flags);
  }

Exit:
  if (binder) {
    script_binder_destroy(binder);
  }
  return exitCode;
}
