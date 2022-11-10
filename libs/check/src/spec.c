#include "check_spec.h"
#include "core_alloc.h"
#include "core_diag.h"
#include "core_time.h"

#include "spec_internal.h"

typedef struct {
  CheckSpecContext api;
  CheckSpec*       spec;
} ContextDiscover;

typedef struct {
  CheckSpecContext  api;
  CheckTestId       testToExec;
  CheckTestContext* testCtx;
} ContextExec;

/**
 * Discovers all tests without executing them.
 */
static CheckTestContext* check_spec_test_discover(CheckSpecContext* ctx, CheckTest test) {
  ContextDiscover* discoverCtx = (ContextDiscover*)ctx;

  discoverCtx->spec->focus |= test.flags & CheckTestFlags_Focus;

  *dynarray_push_t(&discoverCtx->spec->tests, CheckTest) = test;
  return null;
}

/**
 * Execute a specific test in the spec.
 */
static CheckTestContext* check_spec_test_exec(CheckSpecContext* ctx, CheckTest test) {
  ContextExec* execCtx = (ContextExec*)ctx;

  if (test.id != execCtx->testToExec) {
    return null;
  }
  execCtx->testCtx->started = true;
  return execCtx->testCtx;
}

static bool check_assert_handler(String msg, const SourceLoc source, void* context) {
  check_report_error(context, msg, source);
  check_finish(context);
}

bool check_visit_setup(CheckSpecContext* ctx) {
  return (ctx->flags & CheckSpecContextFlags_Setup) != 0;
}

bool check_visit_teardown(CheckSpecContext* ctx) {
  return (ctx->flags & CheckSpecContextFlags_Teardown) != 0;
}

CheckTestContext* check_visit_test(CheckSpecContext* ctx, const CheckTest test) {
  return ctx->visitTest ? ctx->visitTest(ctx, test) : null;
}

CheckSpec check_spec_create(Allocator* alloc, const CheckSpecDef* def) {
  CheckSpec spec = {
      .def   = def,
      .tests = dynarray_create_t(alloc, CheckTest, 64),
  };

  // Discover all tests in this spec-routine.
  ContextDiscover ctx = {.api = {.visitTest = check_spec_test_discover}, .spec = &spec};
  def->routine(&ctx.api);

  return spec;
}

void check_spec_destroy(CheckSpec* spec) { dynarray_destroy(&spec->tests); }

CheckResult* check_exec_test(Allocator* alloc, const CheckSpec* spec, const CheckTestId id) {

  CheckResult*     result    = check_result_create(alloc);
  CheckTestContext testCtx   = {.result = result};
  const TimeSteady startTime = time_steady_clock();

  // Early-out in a test will longjmp here.
  bool finished = setjmp(testCtx.finishJumpDest);
FinishLabel:
  if (finished) {

    // Clear our registered assertion handler.
    diag_set_assert_handler(null, null);

    const TimeSteady   endTime  = time_steady_clock();
    const TimeDuration duration = time_steady_duration(startTime, endTime);

    check_result_finish(result, duration);
    return result;
  }

  // Register an assertion handler to report assertion failures as test errors instead of
  // terminating the program.
  diag_set_assert_handler(check_assert_handler, &testCtx);

  // Execute the specific test.
  ContextExec ctx = {
      .api =
          {.visitTest = check_spec_test_exec,
           .flags     = CheckSpecContextFlags_Setup | CheckSpecContextFlags_Teardown},
      .testToExec = id,
      .testCtx    = &testCtx};
  spec->def->routine(&ctx.api);
  diag_assert_msg(testCtx.started, "Unable to find a test with id: {}", fmt_int(id));

  finished = true;
  goto FinishLabel;
}

void check_report_error(CheckTestContext* ctx, String msg, const SourceLoc source) {
  diag_break(); // Halt when running in a debugger.
  check_result_error(ctx->result, msg, source);
}

void check_eq_u64_raw(CheckTestContext* ctx, const u64 a, const u64 b, const SourceLoc source) {
  if (UNLIKELY(a != b)) {
    check_report_error(ctx, fmt_write_scratch("{} == {}", fmt_int(a), fmt_int(b)), source);
  }
}

void check_eq_i64_raw(CheckTestContext* ctx, const i64 a, const i64 b, const SourceLoc source) {
  if (UNLIKELY(a != b)) {
    check_report_error(ctx, fmt_write_scratch("{} == {}", fmt_int(a), fmt_int(b)), source);
  }
}

void check_eq_f64_raw(
    CheckTestContext* ctx, const f64 a, const f64 b, const f64 threshold, const SourceLoc source) {
  if (UNLIKELY(float_isnan(a))) {
    check_report_error(ctx, fmt_write_scratch("nan == {}", fmt_float(b)), source);
  } else if (UNLIKELY(float_isnan(b))) {
    check_report_error(ctx, fmt_write_scratch("{} == nan", fmt_float(a)), source);
  } else if (UNLIKELY(math_abs(a - b) > threshold)) {
    check_report_error(ctx, fmt_write_scratch("{} == {}", fmt_float(a), fmt_float(b)), source);
  }
}

void check_eq_string_raw(
    CheckTestContext* ctx, const String a, const String b, const SourceLoc source) {
  if (UNLIKELY(!string_eq(a, b))) {
    check_report_error(
        ctx,
        fmt_write_scratch(
            "'{}' == '{}'",
            fmt_text(a, .flags = FormatTextFlags_EscapeNonPrintAscii),
            fmt_text(b, .flags = FormatTextFlags_EscapeNonPrintAscii)),
        source);
  }
}

NORETURN void check_finish(CheckTestContext* ctx) { longjmp(ctx->finishJumpDest, true); }
