#include "ai.h"
#include "ai_tracer_count.h"
#include "asset_behavior.h"
#include "check_spec.h"
#include "core_alloc.h"
#include "core_array.h"

spec(node_sequence) {
  AiBlackboard* bb = null;
  AiTracerCount tracer;

  setup() {
    bb     = ai_blackboard_create(g_alloc_heap);
    tracer = ai_tracer_count();
  }

  it("evaluates to success when it doesn't have any children") {
    const AssetAiNode nodeDefs[] = {
        {
            .type          = AssetAiNode_Sequence,
            .nextSibling   = sentinel_u16,
            .data_sequence = {.childrenBegin = sentinel_u16},
        },
    };
    const AiEvalContext ctx = {
        .memory   = bb,
        .tracer   = &tracer.api,
        .nodeDefs = nodeDefs,
    };
    check(ai_eval(&ctx, AssetAiNodeRoot) == AiResult_Success);
    check_eq_int(tracer.count, 1);
  }

  it("evaluates to success when all children evaluate to success") {
    const AssetAiNode nodeDefs[] = {
        {
            .type          = AssetAiNode_Sequence,
            .nextSibling   = sentinel_u16,
            .data_sequence = {.childrenBegin = 1},
        },
        {.type = AssetAiNode_Success, .nextSibling = 2},
        {.type = AssetAiNode_Success, .nextSibling = 3},
        {.type = AssetAiNode_Success, .nextSibling = sentinel_u16},
    };
    const AiEvalContext ctx = {
        .memory   = bb,
        .tracer   = &tracer.api,
        .nodeDefs = nodeDefs,
    };
    check(ai_eval(&ctx, AssetAiNodeRoot) == AiResult_Success);
    check_eq_int(tracer.count, 4);
  }

  it("evaluates to running when any child evaluates to running") {
    const AssetAiNode nodeDefs[] = {
        {
            .type          = AssetAiNode_Sequence,
            .nextSibling   = sentinel_u16,
            .data_sequence = {.childrenBegin = 1},
        },
        {.type = AssetAiNode_Success, .nextSibling = 2},
        {.type = AssetAiNode_Running, .nextSibling = 3},
        {.type = AssetAiNode_Success, .nextSibling = sentinel_u16},
    };
    const AiEvalContext ctx = {
        .memory   = bb,
        .tracer   = &tracer.api,
        .nodeDefs = nodeDefs,
    };
    check(ai_eval(&ctx, AssetAiNodeRoot) == AiResult_Running);
    check_eq_int(tracer.count, 3);
  }

  it("evaluates to failure when any child evaluates to failure") {
    const AssetAiNode nodeDefs[] = {
        {
            .type          = AssetAiNode_Sequence,
            .nextSibling   = sentinel_u16,
            .data_sequence = {.childrenBegin = 1},
        },
        {.type = AssetAiNode_Success, .nextSibling = 2},
        {.type = AssetAiNode_Failure, .nextSibling = 3},
        {.type = AssetAiNode_Running, .nextSibling = 4},
        {.type = AssetAiNode_Success, .nextSibling = sentinel_u16},
    };
    const AiEvalContext ctx = {
        .memory   = bb,
        .tracer   = &tracer.api,
        .nodeDefs = nodeDefs,
    };
    check(ai_eval(&ctx, AssetAiNodeRoot) == AiResult_Failure);
    check_eq_int(tracer.count, 3);
  }

  teardown() { ai_blackboard_destroy(bb); }
}
