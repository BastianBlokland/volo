#include "ai.h"
#include "ai_tracer_count.h"
#include "asset_behavior.h"
#include "check_spec.h"
#include "core_alloc.h"
#include "core_array.h"

spec(node_selector) {
  AiBlackboard* bb = null;
  AiTracerCount tracer;

  setup() {
    bb     = ai_blackboard_create(g_alloc_heap);
    tracer = ai_tracer_count();
  }

  it("evaluates to failure when it doesn't have any children") {
    const AssetAiNode nodeDef = {
        .type          = AssetAiNode_Selector,
        .data_selector = {.children = {0}},
    };
    const AiEvalContext ctx = {
        .memory = bb,
        .tracer = &tracer.api,
    };
    check(ai_eval(&ctx, &nodeDef) == AiResult_Failure);
    check_eq_int(tracer.count, 1);
  }

  it("evaluates to success when any child evaluates to success") {
    const AssetAiNode children[] = {
        {.type = AssetAiNode_Failure},
        {.type = AssetAiNode_Success},
        {.type = AssetAiNode_Running},
        {.type = AssetAiNode_Failure},
    };
    const AssetAiNode nodeDef = {
        .type          = AssetAiNode_Selector,
        .data_selector = {.children = {.values = children, array_elems(children)}},
    };
    const AiEvalContext ctx = {
        .memory = bb,
        .tracer = &tracer.api,
    };
    check(ai_eval(&ctx, &nodeDef) == AiResult_Success);
    check_eq_int(tracer.count, 3);
  }

  it("evaluates to running when any child evaluates to running") {
    const AssetAiNode children[] = {
        {.type = AssetAiNode_Failure},
        {.type = AssetAiNode_Failure},
        {.type = AssetAiNode_Running},
        {.type = AssetAiNode_Failure},
    };
    const AssetAiNode nodeDef = {
        .type          = AssetAiNode_Selector,
        .data_selector = {.children = {.values = children, array_elems(children)}},
    };
    const AiEvalContext ctx = {
        .memory = bb,
        .tracer = &tracer.api,
    };
    check(ai_eval(&ctx, &nodeDef) == AiResult_Running);
    check_eq_int(tracer.count, 4);
  }

  it("evaluates to failure when all children evaluate to failure") {
    const AssetAiNode children[] = {
        {.type = AssetAiNode_Failure},
        {.type = AssetAiNode_Failure},
        {.type = AssetAiNode_Failure},
    };
    const AssetAiNode nodeDef = {
        .type          = AssetAiNode_Selector,
        .data_selector = {.children = {.values = children, array_elems(children)}},
    };
    const AiEvalContext ctx = {
        .memory = bb,
        .tracer = &tracer.api,
    };
    check(ai_eval(&ctx, &nodeDef) == AiResult_Failure);
    check_eq_int(tracer.count, 4);
  }

  teardown() { ai_blackboard_destroy(bb); }
}
