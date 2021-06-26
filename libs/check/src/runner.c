#include "core_alloc.h"
#include "core_file.h"
#include "core_format.h"
#include "core_path.h"
#include "core_thread.h"

#include "jobs_graph.h"
#include "jobs_scheduler.h"

#include "check_runner.h"

#include "spec_internal.h"

typedef struct {
  const CheckSpec*  spec;
  const CheckBlock* block;
  CheckRunResult*   totalResult;
} CheckRunBlockContext;

static void
check_write_result(const CheckSpec* spec, const CheckBlock* block, CheckResult* result) {
  DynString str = dynstring_create_over(alloc_alloc(g_alloc_scratch, usize_kibibyte * 2, 1));

  const bool      fail       = result->type == CheckResultType_Failure;
  File*           outputFile = fail ? g_file_stderr : g_file_stdout;
  const FormatArg styleColor = tty_isatty(outputFile)
                                   ? fmt_ttystyle(
                                             .fgColor = fail ? TtyFgColor_Red : TtyFgColor_Green,
                                             .flags   = TtyStyleFlags_Bold)
                                   : fmt_nop();
  const FormatArg styleReset = tty_isatty(outputFile) ? fmt_ttystyle() : fmt_nop();

  fmt_write(
      &str,
      "[ {}{<7}{} {<6} ] ",
      styleColor,
      fail ? fmt_text_lit("Failed") : fmt_text_lit("Passed"),
      styleReset,
      fmt_duration(result->duration));

  fmt_write(&str, "{}: {}\n", fmt_text(spec->def->name), fmt_text(block->description));

  dynarray_for_t(&result->errors, CheckError, err, {
    fmt_write(
        &str,
        "{<19}{}->{} {}:{}: {}{}{}\n",
        fmt_nop(),
        styleColor,
        styleReset,
        fmt_path(err->source.file),
        fmt_int(err->source.line),
        styleColor,
        fmt_text(err->msg),
        styleReset);
  });

  file_write_sync(outputFile, dynstring_view(&str));
  dynstring_destroy(&str);
}

static void check_run_block(void* context) {
  CheckRunBlockContext* runCtx = context;

  CheckResult* result = check_exec_block(g_alloc_heap, runCtx->spec, runCtx->block->id);
  check_write_result(runCtx->spec, runCtx->block, result);

  if (result->type == CheckResultType_Failure) {
    // If one block fails mark the entire run as failed.
    *runCtx->totalResult = CheckRunResult_Failure;
  }

  check_result_destroy(result);
}

CheckRunResult check_run(CheckDef* check) {

  String   testSuiteName = path_stem(g_path_executable);
  DynArray specs         = dynarray_create_t(g_alloc_heap, CheckSpec, 64);

  // Gather all test blocks.
  usize totalBlocks = 0;
  dynarray_for_t(&check->specs, CheckSpecDef, specDef, {
    CheckSpec spec = check_spec_create(g_alloc_heap, specDef);
    totalBlocks += spec.blocks.size;
    *dynarray_push_t(&specs, CheckSpec) = spec;
  });

  file_write_sync(
      g_file_stdout,
      fmt_write_scratch(
          "{}: Running {} tests... (pid: {}) \n",
          fmt_text(testSuiteName),
          fmt_int(totalBlocks),
          fmt_int(g_thread_pid)));

  CheckRunResult result = CheckRunResult_Success;

  // Create a job graph with tasks to execute all blocks.
  JobGraph* graph = jobs_graph_create(g_alloc_heap, testSuiteName, totalBlocks);
  dynarray_for_t(&specs, CheckSpec, spec, {
    dynarray_for_t(&spec->blocks, CheckBlock, block, {
      jobs_graph_add_task(
          graph,
          fmt_write_scratch("{}-{}", fmt_text(spec->def->name), fmt_int(block->id)),
          check_run_block,
          mem_struct(CheckRunBlockContext, .spec = spec, .block = block, .totalResult = &result));
    });
  });

  // Execute all tasks.
  jobs_scheduler_wait_help(jobs_scheduler_run(graph));

  file_write_sync(
      g_file_stdout,
      fmt_write_scratch(
          "Finished: {}\n",
          result == CheckRunResult_Failure ? fmt_text_lit("Failed") : fmt_text_lit("Passed")));

  // Cleanup.
  jobs_graph_destroy(graph);
  dynarray_for_t(&specs, CheckSpec, spec, { check_spec_destroy(spec); });
  dynarray_destroy(&specs);
  return result;
}
