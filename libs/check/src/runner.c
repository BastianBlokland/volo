#include "check_runner.h"
#include "core_alloc.h"
#include "core_array.h"
#include "core_file.h"
#include "core_thread.h"
#include "jobs_graph.h"
#include "jobs_scheduler.h"
#include "log_logger.h"

#include "output_log_internal.h"
#include "output_mocha_internal.h"
#include "output_pretty_internal.h"
#include "spec_internal.h"

typedef CheckOutput* CheckOutputPtr;

typedef struct {
  CheckOutputPtr* outputs;
  usize           outputsCount;
  i64             numFailedTests;
} CheckRunContext;

typedef struct {
  const CheckSpec* spec;
  const CheckTest* test;
  CheckRunContext* ctx;
} CheckTaskData;

static void check_test_task(const void* context) {
  const CheckTaskData* data = context;

  // Execute the test.
  CheckResult*          result = check_exec_test(g_allocHeap, data->spec, data->test->id);
  const CheckResultType type   = result->errors.size ? CheckResultType_Fail : CheckResultType_Pass;

  // Report the result.
  for (usize i = 0; i != data->ctx->outputsCount; ++i) {
    CheckOutput* out = data->ctx->outputs[i];
    out->testFinished(out, data->spec, data->test, type, result);
  }
  if (type == CheckResultType_Fail) {
    thread_atomic_add_i64(&data->ctx->numFailedTests, 1);
  }

  check_result_destroy(result);
}

CheckResultType check_run(CheckDef* check, const CheckRunFlags flags) {
  const TimeSteady startTime = time_steady_clock();

  // Setup outputs.
  CheckOutputPtr outputs[] = {
      check_output_pretty(g_allocHeap, g_file_stdout, flags),
      check_output_mocha_default(g_allocHeap),
      check_output_log(g_allocHeap, g_logger),
  };
  CheckRunContext ctx = {.outputs = outputs, .outputsCount = array_elems(outputs)};

  array_for_t(outputs, CheckOutputPtr, out) { (*out)->runStarted(*out); }

  // Discover all tests.
  DynArray specs    = dynarray_create_t(g_allocHeap, CheckSpec, 64);
  bool     focus    = false;
  u32      numTests = 0;
  dynarray_for_t(&check->specs, CheckSpecDef, specDef) {
    CheckSpec spec = check_spec_create(g_allocHeap, specDef);
    focus |= spec.focus;
    numTests += (u32)spec.tests.size;
    *dynarray_push_t(&specs, CheckSpec) = spec;
  }

  const TimeDuration discoveryTime = time_steady_duration(startTime, time_steady_clock());
  array_for_t(outputs, CheckOutputPtr, out) {
    (*out)->testsDiscovered(*out, specs.size, numTests, discoveryTime);
  }

  // Create a job graph with tasks to execute all tests.
  JobGraph* graph      = jobs_graph_create(g_allocHeap, string_lit("tests"), numTests);
  usize     numSkipped = 0;
  dynarray_for_t(&specs, CheckSpec, spec) {
    // Create tasks to execute all tests in the spec.
    dynarray_for_t(&spec->tests, CheckTest, test) {
      if (test->flags & CheckTestFlags_Skip || (focus && !(test->flags & CheckTestFlags_Focus))) {
        ++numSkipped;
        array_for_t(outputs, CheckOutputPtr, out) { (*out)->testSkipped(*out, spec, test); }
        continue;
      }
      jobs_graph_add_task(
          graph,
          fmt_write_scratch("{}-{}", fmt_text(spec->def->name), fmt_int(test->id)),
          check_test_task,
          mem_struct(CheckTaskData, .spec = spec, .test = test, .ctx = &ctx),
          JobTaskFlags_None);
    }
  }

  // Execute all tasks.
  jobs_scheduler_wait_help(jobs_scheduler_run(graph, g_allocPage));

  // Observe the results.
  const usize           numFailed  = ctx.numFailedTests;
  const usize           numPassed  = numTests - numSkipped - numFailed;
  const CheckResultType resultType = numFailed ? CheckResultType_Fail : CheckResultType_Pass;
  const TimeDuration    runTime    = time_steady_duration(startTime, time_steady_clock());

  array_for_t(outputs, CheckOutputPtr, out) {
    (*out)->runFinished(*out, resultType, runTime, numPassed, numFailed, numSkipped);
  }

  // Cleanup.
  jobs_graph_destroy(graph);
  dynarray_for_t(&specs, CheckSpec, spec) { check_spec_destroy(spec); }
  dynarray_destroy(&specs);

  array_for_t(outputs, CheckOutputPtr, out) { (*out)->destroy(*out); }
  return resultType;
}
