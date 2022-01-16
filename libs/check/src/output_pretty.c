#include "core_alloc.h"
#include "core_path.h"
#include "core_thread.h"
#include "core_tty.h"
#include "jobs_executor.h"

#include "output_pretty_internal.h"

typedef struct {
  CheckOutput   api;
  Allocator*    alloc;
  File*         file;
  String        suiteName;
  CheckRunFlags runFlags;
  bool          style;
} CheckOutputPretty;

static FormatArg arg_style_bold(CheckOutputPretty* prettyOut) {
  return prettyOut->style ? fmt_ttystyle(.flags = TtyStyleFlags_Bold) : fmt_nop();
}

static FormatArg arg_style_dim(CheckOutputPretty* prettyOut) {
  return prettyOut->style ? fmt_ttystyle(.flags = TtyStyleFlags_Faint) : fmt_nop();
}

static FormatArg arg_style_reset(CheckOutputPretty* prettyOut) {
  return prettyOut->style ? fmt_ttystyle() : fmt_nop();
}

static FormatArg arg_style_result(CheckOutputPretty* prettyOut, const CheckResultType result) {
  const TtyFgColor color =
      result == CheckResultType_Pass ? TtyFgColor_BrightGreen : TtyFgColor_BrightRed;
  return prettyOut->style ? fmt_ttystyle(.fgColor = color, .flags = TtyStyleFlags_Bold) : fmt_nop();
}

static void output_write(CheckOutputPretty* prettyOut, const String str) {
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

static void output_tests_discovered(
    CheckOutput* out, const usize specCount, const usize testCount, const TimeDuration dur) {
  CheckOutputPretty* prettyOut = (CheckOutputPretty*)out;
  (void)specCount;

  const String str = fmt_write_scratch(
      "> Discovered {}{}{} tests. {}({}){}\n",
      arg_style_bold(prettyOut),
      fmt_int(testCount),
      arg_style_reset(prettyOut),
      arg_style_dim(prettyOut),
      fmt_duration(dur),
      arg_style_reset(prettyOut));

  output_write(prettyOut, str);
}

static void output_test_skipped(CheckOutput* out, const CheckSpec* spec, const CheckTest* test) {
  (void)out;
  (void)spec;
  (void)test;
}

static void output_test_finished(
    CheckOutput*          out,
    const CheckSpec*      spec,
    const CheckTest*      test,
    const CheckResultType type,
    CheckResult*          result) {
  CheckOutputPretty* prettyOut = (CheckOutputPretty*)out;

  if (!(prettyOut->runFlags & CheckRunFlags_OutputPassingTests) && type != CheckResultType_Fail) {
    return;
  }

  DynString str = dynstring_create(g_alloc_heap, 1024);
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
      fmt_text(test->description),
      arg_style_dim(prettyOut),
      fmt_duration(result->duration),
      arg_style_reset(prettyOut));

  dynarray_for_t(&result->errors, CheckError, err) {
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
  }

  output_write(prettyOut, dynstring_view(&str));
  dynstring_destroy(&str);
}

static String output_run_stats_str(const TimeDuration dur) {
  return fmt_write_scratch("{}, {}", fmt_duration(dur), fmt_size(alloc_stats_total()));
}

static void output_run_finished(
    CheckOutput*          out,
    const CheckResultType type,
    const TimeDuration    dur,
    const usize           numPassed,
    const usize           numFailed,
    const usize           numSkipped) {
  CheckOutputPretty* prettyOut = (CheckOutputPretty*)out;

  const String str = fmt_write_scratch(
      "> Finished: {}{}{} [Passed: {}, Failed: {}, Skipped: {}] {}({}){}\n",
      arg_style_result(prettyOut, type),
      type == CheckResultType_Pass ? fmt_text_lit("PASS") : fmt_text_lit("FAIL"),
      arg_style_reset(prettyOut),
      fmt_int(numPassed),
      fmt_int(numFailed),
      fmt_int(numSkipped),
      arg_style_dim(prettyOut),
      fmt_text(output_run_stats_str(dur)),
      arg_style_reset(prettyOut));

  output_write(prettyOut, str);
}

static void output_destroy(CheckOutput* out) {
  CheckOutputPretty* prettyOut = (CheckOutputPretty*)out;
  alloc_free_t(prettyOut->alloc, prettyOut);
}

CheckOutput* check_output_pretty(Allocator* alloc, File* file, const CheckRunFlags runFlags) {
  CheckOutputPretty* prettyOut = alloc_alloc_t(alloc, CheckOutputPretty);
  *prettyOut                   = (CheckOutputPretty){
      .api =
          {
              .runStarted      = output_run_started,
              .testsDiscovered = output_tests_discovered,
              .testSkipped     = output_test_skipped,
              .testFinished    = output_test_finished,
              .runFinished     = output_run_finished,
              .destroy         = output_destroy,
          },
      .alloc     = alloc,
      .file      = file,
      .suiteName = path_stem(g_path_executable),
      .runFlags  = runFlags,
      .style     = tty_isatty(file),
  };
  return (CheckOutput*)prettyOut;
}
