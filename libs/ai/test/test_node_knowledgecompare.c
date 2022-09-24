#include "ai.h"
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
    ai_blackboard_set_bool(bb, string_hash_lit("test"), true);

    const AssetBehavior behavior = {
        .type = AssetBehavior_KnowledgeCompare,
        .data_knowledgecompare =
            {
                .comparison = AssetKnowledgeComparison_Equal,
                .key        = string_lit("test"),
                .value      = {.type = AssetKnowledgeSource_Bool, .data_bool.value = true},
            },
    };
    check(ai_eval(&behavior, bb, &tracer.api) == AiResult_Success);
    check_eq_int(tracer.count, 1);
  }

  it("evaluates to failure when the key does not exist") {
    const AssetBehavior behavior = {
        .type = AssetBehavior_KnowledgeCompare,
        .data_knowledgecompare =
            {
                .comparison = AssetKnowledgeComparison_Equal,
                .key        = string_lit("test"),
                .value      = {.type = AssetKnowledgeSource_Bool, .data_bool.value = true},
            },
    };
    check(ai_eval(&behavior, bb, &tracer.api) == AiResult_Failure);
    check_eq_int(tracer.count, 1);
  }

  it("evaluates to failure when equals comparison fails") {
    ai_blackboard_set_bool(bb, string_hash_lit("test"), false);

    const AssetBehavior behavior = {
        .type = AssetBehavior_KnowledgeCompare,
        .data_knowledgecompare =
            {
                .comparison = AssetKnowledgeComparison_Equal,
                .key        = string_lit("test"),
                .value      = {.type = AssetKnowledgeSource_Bool, .data_bool.value = true},
            },
    };
    check(ai_eval(&behavior, bb, &tracer.api) == AiResult_Failure);
    check_eq_int(tracer.count, 1);
  }

  teardown() { ai_blackboard_destroy(bb); }
}
