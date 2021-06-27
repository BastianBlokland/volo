#include "output.pretty.h"

typedef struct {
  CheckOutput api;
  Allocator*  alloc;
  File*       file;
} CheckOutputPretty;

static void output_run_started(CheckOutput* out) { (void)out; }

static void output_tests_discovered(CheckOutput* out, usize count, TimeDuration dur) {
  (void)out;
  (void)count;
  (void)dur;
}

static void output_test_finished(
    CheckOutput*      out,
    const CheckSpec*  spec,
    const CheckBlock* block,
    CheckResultType   type,
    CheckResult*      result) {
  (void)out;
  (void)spec;
  (void)block;
  (void)type;
  (void)result;
}

static void output_run_finished(
    CheckOutput* out, CheckResultType type, TimeDuration dur, usize numFailed, usize numPassed) {
  (void)out;
  (void)type;
  (void)dur;
  (void)numFailed;
  (void)numPassed;
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
      .alloc = alloc,
      .file  = file,
  };
  return (CheckOutput*)prettyOut;
}
