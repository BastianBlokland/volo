#include "ai.h"
#include "ai_tracer_count.h"
#include "asset_behavior.h"
#include "check_spec.h"
#include "core_alloc.h"

#include "utils_internal.h"

spec(node_repeat) {
  AiBlackboard* bb = null;
  AiTracerCount tracer;

  setup() {
    bb     = ai_blackboard_create(g_alloc_heap);
    tracer = ai_tracer_count();
  }

  it("evaluates to running when child evaluates to running") {
    const AssetAiNode child   = {.type = AssetAiNode_Running};
    const AssetAiNode nodeDef = {
        .type        = AssetAiNode_Repeat,
        .data_repeat = {.child = &child},
    };
    const AiEvalContext ctx = {
        .memory = bb,
        .tracer = &tracer.api,
    };
    check(ai_eval(&ctx, &nodeDef) == AiResult_Running);
    check_eq_int(tracer.count, 2);
  }

  it("evaluates to running when child evaluates to success") {
    const AssetAiNode child   = {.type = AssetAiNode_Success};
    const AssetAiNode nodeDef = {
        .type        = AssetAiNode_Repeat,
        .data_repeat = {.child = &child},
    };
    const AiEvalContext ctx = {
        .memory = bb,
        .tracer = &tracer.api,
    };
    check(ai_eval(&ctx, &nodeDef) == AiResult_Running);
    check_eq_int(tracer.count, 2);
  }

  it("evaluates to failure when child evaluates to failure") {
    const AssetAiNode child   = {.type = AssetAiNode_Failure};
    const AssetAiNode nodeDef = {
        .type        = AssetAiNode_Repeat,
        .data_repeat = {.child = &child},
    };
    const AiEvalContext ctx = {
        .memory = bb,
        .tracer = &tracer.api,
    };
    check(ai_eval(&ctx, &nodeDef) == AiResult_Failure);
    check_eq_int(tracer.count, 2);
  }

  it("always evaluates the child node") {
    const AssetAiNode child = {
        .type = AssetAiNode_KnowledgeSet,
        .data_knowledgeset =
            {
                .key   = string_lit("test"),
                .value = {.type = AssetAiSource_Number, .data_number.value = 42.42},
            },
    };
    const AssetAiNode nodeDef = {
        .type        = AssetAiNode_Repeat,
        .data_repeat = {.child = &child},
    };
    const AiEvalContext ctx = {
        .memory = bb,
        .tracer = &tracer.api,
    };
    check(ai_eval(&ctx, &nodeDef) == AiResult_Running);
    check_eq_int(tracer.count, 2);
    check_eq_value(ai_blackboard_get(bb, string_hash_lit("test")), ai_value_f64(42.42));
  }

  teardown() { ai_blackboard_destroy(bb); }
}
