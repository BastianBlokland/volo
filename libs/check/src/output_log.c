#include "core_path.h"
#include "core_thread.h"

#include "jobs_executor.h"

#include "log_logger.h"

#include "output_log_internal.h"

typedef struct {
  CheckOutput api;
  Allocator*  alloc;
  Logger*     logger;
} CheckOutputLog;

static void output_run_started(CheckOutput* out) {
  CheckOutputLog* logOut = (CheckOutputLog*)out;

  log(logOut->logger,
      LogLevel_Info,
      "Starting test run",
      log_param("pid", fmt_int(g_thread_pid)),
      log_param("workers", fmt_int(g_jobsWorkerCount)),
      log_param("executable", fmt_path(g_path_executable)));
}

static void output_tests_discovered(
    CheckOutput* out, const usize specCount, const usize testCount, const TimeDuration dur) {
  CheckOutputLog* logOut = (CheckOutputLog*)out;

  log(logOut->logger,
      LogLevel_Debug,
      "Test discovery complete",
      log_param("spec-count", fmt_int(specCount)),
      log_param("test-count", fmt_int(testCount)),
      log_param("duration", fmt_duration(dur)));
}

static void output_test_skipped(CheckOutput* out, const CheckSpec* spec, const CheckTest* test) {
  CheckOutputLog* logOut = (CheckOutputLog*)out;

  log(logOut->logger,
      LogLevel_Info,
      "Test skipped",
      log_param("spec", fmt_text(spec->def->name)),
      log_param("test", fmt_text(test->description)));
}

static void output_test_finished(
    CheckOutput*          out,
    const CheckSpec*      spec,
    const CheckTest*      test,
    const CheckResultType type,
    CheckResult*          result) {
  CheckOutputLog* logOut = (CheckOutputLog*)out;

  log(logOut->logger,
      LogLevel_Info,
      "Test finished",
      log_param("spec", fmt_text(spec->def->name)),
      log_param("test", fmt_text(test->description)),
      log_param(
          "result", type == CheckResultType_Pass ? fmt_text_lit("pass") : fmt_text_lit("fail")),
      log_param("duration", fmt_duration(result->duration)));

  dynarray_for_t(&result->errors, CheckError, err, {
    log(logOut->logger,
        LogLevel_Error,
        "Test check failure",
        log_param("message", fmt_text(err->msg)),
        log_param("source-file", fmt_path(err->source.file)),
        log_param("source-line", fmt_int(err->source.line)));
  });
}

static void output_run_finished(
    CheckOutput*          out,
    const CheckResultType type,
    const TimeDuration    dur,
    const usize           numPassed,
    const usize           numFailed,
    const usize           numSkipped) {
  CheckOutputLog* logOut = (CheckOutputLog*)out;

  log(logOut->logger,
      LogLevel_Info,
      "Finished test run",
      log_param("passed", fmt_int(numPassed)),
      log_param("failed", fmt_int(numFailed)),
      log_param("skipped", fmt_int(numSkipped)),
      log_param(
          "result", type == CheckResultType_Pass ? fmt_text_lit("pass") : fmt_text_lit("fail")),
      log_param("duration", fmt_duration(dur)));
}

static void output_destroy(CheckOutput* out) {
  CheckOutputLog* logOut = (CheckOutputLog*)out;
  alloc_free_t(logOut->alloc, logOut);
}

CheckOutput* check_output_log(Allocator* alloc, Logger* logger) {
  CheckOutputLog* logOut = alloc_alloc_t(alloc, CheckOutputLog);
  *logOut                = (CheckOutputLog){
      .api =
          {
              .runStarted      = output_run_started,
              .testsDiscovered = output_tests_discovered,
              .testSkipped     = output_test_skipped,
              .testFinished    = output_test_finished,
              .runFinished     = output_run_finished,
              .destroy         = output_destroy,
          },
      .alloc  = alloc,
      .logger = logger,
  };
  return (CheckOutput*)logOut;
}
