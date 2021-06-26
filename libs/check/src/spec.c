#include "core_alloc.h"
#include "core_diag.h"
#include "core_time.h"

#include "spec_internal.h"

typedef struct {
  CheckSpecContext api;
  CheckSpec*       spec;
} ContextDiscover;

typedef struct {
  CheckSpecContext   api;
  CheckBlockId       blockToExec;
  CheckBlockContext* blockCtx;
} ContextExec;

/**
 * Discovers all spec-blocks without executing them.
 */
static CheckBlockContext* check_spec_block_discover(CheckSpecContext* ctx, CheckBlock block) {
  ContextDiscover* discoverCtx = (ContextDiscover*)ctx;

  *dynarray_push_t(&discoverCtx->spec->blocks, CheckBlock) = block;
  return null;
}

/**
 * Execute a specific block in the spec.
 */
static CheckBlockContext* check_spec_block_exec(CheckSpecContext* ctx, CheckBlock block) {
  ContextExec* execCtx = (ContextExec*)ctx;

  if (block.id != execCtx->blockToExec) {
    return null;
  }
  execCtx->blockCtx->started = true;
  return execCtx->blockCtx;
}

CheckBlockContext* check_visit_block(CheckSpecContext* ctx, CheckBlock block) {
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

  CheckResult*      result    = check_result_create(alloc);
  CheckBlockContext blockCtx  = {.result = result};
  const TimeSteady  startTime = time_steady_clock();

  // Finishing an exec block will longjmp here.
  const CheckResultType resultType = setjmp(blockCtx.finishJumpDest);
  if (resultType != CheckResultType_None) {

    const TimeSteady   endTime  = time_steady_clock();
    const TimeDuration duration = time_steady_duration(startTime, endTime);

    check_result_finish(result, resultType, duration);
    return result;
  }

  // Execute the specific block.
  ContextExec ctx = {.api = {check_spec_block_exec}, .blockToExec = id, .blockCtx = &blockCtx};
  spec->def->routine(&ctx.api);
  diag_assert_msg(blockCtx.started, "Unable to find a block with id: {}", fmt_int(id));

  // Not calling 'check_finish_failure' or 'check_finish_success' is considered a success too.
  check_finish_success(&blockCtx);
}

void check_report_error(CheckBlockContext* ctx, String msg, const SourceLoc source) {
  check_result_error(ctx->result, msg, source);
}

NORETURN void check_finish_failure(CheckBlockContext* ctx) {
  longjmp(ctx->finishJumpDest, CheckResultType_Failure);
}

NORETURN void check_finish_success(CheckBlockContext* ctx) {
  longjmp(ctx->finishJumpDest, CheckResultType_Success);
}
