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
    const AssetAiNode nodeDef = {
        .type          = AssetAiNode_Sequence,
        .data_sequence = {.children = {0}},
    };
    check(ai_eval(&nodeDef, bb, &tracer.api) == AiResult_Success);
    check_eq_int(tracer.count, 1);
  }

  it("evaluates to success when all children evaluate to success") {
    const AssetAiNode children[] = {
        {.type = AssetAiNode_Success},
        {.type = AssetAiNode_Success},
        {.type = AssetAiNode_Success},
    };
    const AssetAiNode nodeDef = {
        .type          = AssetAiNode_Sequence,
        .data_sequence = {.children = {.values = children, array_elems(children)}},
    };
    check(ai_eval(&nodeDef, bb, &tracer.api) == AiResult_Success);
    check_eq_int(tracer.count, 4);
  }

  it("evaluates to running when any child evaluates to running") {
    const AssetAiNode children[] = {
        {.type = AssetAiNode_Success},
        {.type = AssetAiNode_Running},
        {.type = AssetAiNode_Success},
    };
    const AssetAiNode nodeDef = {
        .type          = AssetAiNode_Sequence,
        .data_sequence = {.children = {.values = children, array_elems(children)}},
    };
    check(ai_eval(&nodeDef, bb, &tracer.api) == AiResult_Running);
    check_eq_int(tracer.count, 3);
  }

  it("evaluates to failure when any child evaluates to failure") {
    const AssetAiNode children[] = {
        {.type = AssetAiNode_Success},
        {.type = AssetAiNode_Failure},
        {.type = AssetAiNode_Running},
        {.type = AssetAiNode_Success},
    };
    const AssetAiNode nodeDef = {
        .type          = AssetAiNode_Sequence,
        .data_sequence = {.children = {.values = children, array_elems(children)}},
    };
    check(ai_eval(&nodeDef, bb, &tracer.api) == AiResult_Failure);
    check_eq_int(tracer.count, 3);
  }

  teardown() { ai_blackboard_destroy(bb); }
}
