#include "ai.h"
#include "ai_tracer_count.h"
#include "asset_behavior.h"
#include "check_spec.h"
#include "core_alloc.h"

spec(node_knowledgecompare) {
  AiBlackboard* bb = null;
  AiTracerCount tracer;

  setup() {
    bb     = ai_blackboard_create(g_alloc_heap);
    tracer = ai_tracer_count();
  }

  it("evaluates to success when equals comparison succeeds") {
    ai_blackboard_set(bb, string_hash_lit("test"), ai_value_bool(true));

    const AssetAiNode nodeDef = {
        .type = AssetAiNode_KnowledgeCompare,
        .data_knowledgecompare =
            {
                .comparison = AssetAiComparison_Equal,
                .key        = string_lit("test"),
                .value      = {.type = AssetAiSource_Bool, .data_bool = true},
            },
    };
    const AiEvalContext ctx = {
        .memory = bb,
        .tracer = &tracer.api,
    };
    check(ai_eval(&ctx, &nodeDef) == AiResult_Success);
    check_eq_int(tracer.count, 1);
  }

  it("evaluates to failure when the key does not exist") {
    const AssetAiNode nodeDef = {
        .type = AssetAiNode_KnowledgeCompare,
        .data_knowledgecompare =
            {
                .comparison = AssetAiComparison_Equal,
                .key        = string_lit("test"),
                .value      = {.type = AssetAiSource_Bool, .data_bool = true},
            },
    };
    const AiEvalContext ctx = {
        .memory = bb,
        .tracer = &tracer.api,
    };
    check(ai_eval(&ctx, &nodeDef) == AiResult_Failure);
    check_eq_int(tracer.count, 1);
  }

  it("evaluates to failure when equals comparison fails") {
    ai_blackboard_set(bb, string_hash_lit("test"), ai_value_bool(false));

    const AssetAiNode nodeDef = {
        .type = AssetAiNode_KnowledgeCompare,
        .data_knowledgecompare =
            {
                .comparison = AssetAiComparison_Equal,
                .key        = string_lit("test"),
                .value      = {.type = AssetAiSource_Bool, .data_bool = true},
            },
    };
    const AiEvalContext ctx = {
        .memory = bb,
        .tracer = &tracer.api,
    };
    check(ai_eval(&ctx, &nodeDef) == AiResult_Failure);
    check_eq_int(tracer.count, 1);
  }

  it("evaluates to success when less comparison succeeds") {
    ai_blackboard_set(bb, string_hash_lit("test"), ai_value_f64(42));

    const AssetAiNode nodeDef = {
        .type = AssetAiNode_KnowledgeCompare,
        .data_knowledgecompare =
            {
                .comparison = AssetAiComparison_Less,
                .key        = string_lit("test"),
                .value      = {.type = AssetAiSource_Number, .data_number.value = 1337},
            },
    };
    const AiEvalContext ctx = {
        .memory = bb,
        .tracer = &tracer.api,
    };
    check(ai_eval(&ctx, &nodeDef) == AiResult_Success);
    check_eq_int(tracer.count, 1);
  }

  it("evaluates to failure when less comparison fails") {
    ai_blackboard_set(bb, string_hash_lit("test"), ai_value_f64(42));
    ai_blackboard_set(bb, string_hash_lit("value"), ai_value_f64(10));

    const AssetAiNode nodeDef = {
        .type = AssetAiNode_KnowledgeCompare,
        .data_knowledgecompare =
            {
                .comparison = AssetAiComparison_Less,
                .key        = string_lit("test"),
                .value =
                    {
                        .type               = AssetAiSource_Knowledge,
                        .data_knowledge.key = string_lit("value"),
                    },
            },
    };
    const AiEvalContext ctx = {
        .memory = bb,
        .tracer = &tracer.api,
    };
    check(ai_eval(&ctx, &nodeDef) == AiResult_Failure);
    check_eq_int(tracer.count, 1);
  }

  teardown() { ai_blackboard_destroy(bb); }
}
