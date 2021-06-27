#include "core_path.h"
#include "core_thread.h"
#include "core_tty.h"

#include "jobs_executor.h"

#include "output.pretty.h"

typedef enum {
  CheckOutputFlags_None  = 0,
  CheckOutputFlags_Style = 1 << 0,
} CheckOutputFlags;

typedef struct {
  CheckOutput      api;
  Allocator*       alloc;
  File*            file;
  String           suiteName;
  CheckOutputFlags flags;
} CheckOutputPretty;

static FormatArg arg_style_bold(CheckOutputPretty* prettyOut) {
  return prettyOut->flags & CheckOutputFlags_Style ? fmt_ttystyle(.flags = TtyStyleFlags_Bold)
                                                   : fmt_nop();
}

static FormatArg arg_style_dim(CheckOutputPretty* prettyOut) {
  return prettyOut->flags & CheckOutputFlags_Style ? fmt_ttystyle(.flags = TtyStyleFlags_Faint)
                                                   : fmt_nop();
}

static FormatArg arg_style_reset(CheckOutputPretty* prettyOut) {
  return prettyOut->flags & CheckOutputFlags_Style ? fmt_ttystyle() : fmt_nop();
}

static FormatArg arg_style_result(CheckOutputPretty* prettyOut, CheckResultType result) {
  const TtyFgColor color =
      result == CheckResultType_Pass ? TtyFgColor_BrightGreen : TtyFgColor_BrightRed;
  return prettyOut->flags & CheckOutputFlags_Style
             ? fmt_ttystyle(.fgColor = color, .flags = TtyStyleFlags_Bold)
             : fmt_nop();
}

static void output_write(CheckOutputPretty* prettyOut, String str) {
  file_write_sync(prettyOut->file, str);
}

static void output_run_started(CheckOutput* out) {
  CheckOutputPretty* prettyOut = (CheckOutputPretty*)out;

  const String str = fmt_write_scratch(
      "{}{}{}: Starting test run. {}(pid: {}, workers: {}){}\n",
      arg_style_bold(prettyOut),
      fmt_text(prettyOut->suiteName),
      arg_style_reset(prettyOut),
      arg_style_dim(prettyOut),
      fmt_int(g_thread_pid),
      fmt_int(g_jobsWorkerCount),
      arg_style_reset(prettyOut));

  output_write(prettyOut, str);
}

static void output_tests_discovered(CheckOutput* out, usize count, TimeDuration dur) {
  CheckOutputPretty* prettyOut = (CheckOutputPretty*)out;

  const String str = fmt_write_scratch(
      "> Discovered {}{}{} tests. {}({}){}\n",
      arg_style_bold(prettyOut),
      fmt_int(count),
      arg_style_reset(prettyOut),
      arg_style_dim(prettyOut),
      fmt_duration(dur),
      arg_style_reset(prettyOut));

  output_write(prettyOut, str);
}

static void output_test_finished(
    CheckOutput*      out,
    const CheckSpec*  spec,
    const CheckBlock* block,
    CheckResultType   type,
    CheckResult*      result) {

  CheckOutputPretty* prettyOut = (CheckOutputPretty*)out;
  DynString str = dynstring_create_over(alloc_alloc(g_alloc_scratch, usize_kibibyte * 2, 1));

  fmt_write(
      &str,
      "* {}{}{}: ",
      arg_style_result(prettyOut, type),
      type == CheckResultType_Pass ? fmt_text_lit("PASS") : fmt_text_lit("FAIL"),
      arg_style_reset(prettyOut));

  fmt_write(
      &str,
      "{}{}{}: {}. {}({}){}\n",
      arg_style_bold(prettyOut),
      fmt_text(spec->def->name),
      arg_style_reset(prettyOut),
      fmt_text(block->description),
      arg_style_dim(prettyOut),
      fmt_duration(result->duration),
      arg_style_reset(prettyOut));

  dynarray_for_t(&result->errors, CheckError, err, {
    fmt_write(
        &str,
        "  {}{}{} {}[file: {} line: {}]{}\n",
        arg_style_result(prettyOut, type),
        fmt_text(err->msg),
        arg_style_reset(prettyOut),
        arg_style_dim(prettyOut),
        fmt_path(err->source.file),
        fmt_int(err->source.line),
        arg_style_reset(prettyOut));
  });

  output_write(prettyOut, dynstring_view(&str));
}

static void output_run_finished(
    CheckOutput* out, CheckResultType type, TimeDuration dur, usize numFailed, usize numPassed) {

  CheckOutputPretty* prettyOut = (CheckOutputPretty*)out;

  const String str = fmt_write_scratch(
      "> Finished: {}{}{} [Passed: {} Failed: {}] {}({}){}\n",
      arg_style_result(prettyOut, type),
      type == CheckResultType_Pass ? fmt_text_lit("PASS") : fmt_text_lit("FAIL"),
      arg_style_reset(prettyOut),
      fmt_int(numPassed),
      fmt_int(numFailed),
      arg_style_dim(prettyOut),
      fmt_duration(dur),
      arg_style_reset(prettyOut));

  output_write(prettyOut, str);
}

static void output_destroy(CheckOutput* out) {
  CheckOutputPretty* prettyOut = (CheckOutputPretty*)out;
  alloc_free_t(prettyOut->alloc, prettyOut);
}

CheckOutput* check_output_pretty_create(Allocator* alloc, File* file) {
  CheckOutputPretty* prettyOut = alloc_alloc_t(alloc, CheckOutputPretty);
  *prettyOut                   = (CheckOutputPretty){
      .api =
          {
              .runStarted      = output_run_started,
              .testsDiscovered = output_tests_discovered,
              .testFinished    = output_test_finished,
              .runFinished     = output_run_finished,
              .destroy         = output_destroy,
          },
      .alloc     = alloc,
      .file      = file,
      .suiteName = path_stem(g_path_executable),
      .flags     = CheckOutputFlags_None |
               (tty_isatty(file) ? CheckOutputFlags_Style : CheckOutputFlags_None),
  };
  return (CheckOutput*)prettyOut;
}
