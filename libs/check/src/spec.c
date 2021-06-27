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
  CheckBlockId      blockToExec;
  CheckTestContext* blockCtx;
} ContextExec;

/**
 * Discovers all spec-blocks without executing them.
 */
static CheckTestContext* check_spec_block_discover(CheckSpecContext* ctx, CheckBlock block) {
  ContextDiscover* discoverCtx = (ContextDiscover*)ctx;

  *dynarray_push_t(&discoverCtx->spec->blocks, CheckBlock) = block;
  return null;
}

/**
 * Execute a specific block in the spec.
 */
static CheckTestContext* check_spec_block_exec(CheckSpecContext* ctx, CheckBlock block) {
  ContextExec* execCtx = (ContextExec*)ctx;

  if (block.id != execCtx->blockToExec) {
    return null;
  }
  execCtx->blockCtx->started = true;
  return execCtx->blockCtx;
}

static bool check_assert_handler(String msg, const SourceLoc source, void* context) {
  check_report_error(context, msg, source);
  return true; // Indicate that we've handled the assertion (so the application keeps going).
}

CheckTestContext* check_visit_block(CheckSpecContext* ctx, CheckBlock block) {
  return ctx->visitBlock(ctx, block);
}

CheckSpec check_spec_create(Allocator* alloc, const CheckSpecDef* def) {
  CheckSpec spec = {
      .def    = def,
      .blocks = dynarray_create_t(alloc, CheckBlock, 64),
  };

  // Discover all blocks in this spec-routine.
  ContextDiscover ctx = {.api = {check_spec_block_discover}, .spec = &spec};
  def->routine(&ctx.api);

  return spec;
}

void check_spec_destroy(CheckSpec* spec) { dynarray_destroy(&spec->blocks); }

CheckResult* check_exec_block(Allocator* alloc, const CheckSpec* spec, const CheckBlockId id) {

  CheckResult*     result    = check_result_create(alloc);
  CheckTestContext testCtx   = {.result = result};
  const TimeSteady startTime = time_steady_clock();

  // Early-out in an exec block will longjmp here.
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

  // Execute the specific block.
  ContextExec ctx = {.api = {check_spec_block_exec}, .blockToExec = id, .blockCtx = &testCtx};
  spec->def->routine(&ctx.api);
  diag_assert_msg(testCtx.started, "Unable to find a block with id: {}", fmt_int(id));

  finished = true;
  goto FinishLabel;
}

void check_report_error(CheckTestContext* ctx, String msg, const SourceLoc source) {
  check_result_error(ctx->result, msg, source);
}

NORETURN void check_finish(CheckTestContext* ctx) { longjmp(ctx->finishJumpDest, true); }
