#include "ai.h"
#include "asset_behavior.h"
#include "check_spec.h"
#include "core_alloc.h"

spec(node_invert) {
  AiBlackboard* bb = null;

  setup() { bb = ai_blackboard_create(g_alloc_heap); }

  it("evaluates to success when child evaluates to failure") {
    const AssetBehavior child    = {.type = AssetBehaviorType_Failure};
    const AssetBehavior behavior = {
        .type        = AssetBehaviorType_Invert,
        .data_invert = {.child = &child},
    };
    check(ai_eval(&behavior, bb) == AiResult_Success);
  }

  it("evaluates to failure when child evaluates to success") {
    const AssetBehavior child    = {.type = AssetBehaviorType_Success};
    const AssetBehavior behavior = {
        .type        = AssetBehaviorType_Invert,
        .data_invert = {.child = &child},
    };
    check(ai_eval(&behavior, bb) == AiResult_Failure);
  }

  it("always evaluates the child node") {
    const AssetBehavior child = {
        .type = AssetBehaviorType_KnowledgeSet,
        .data_knowledgeset =
            {
                .key   = string_lit("test"),
                .value = {.type = AssetKnowledgeType_f64, .data_f64 = 42.42},
            },
    };
    const AssetBehavior behavior = {
        .type        = AssetBehaviorType_Invert,
        .data_invert = {.child = &child},
    };
    check(ai_eval(&behavior, bb) == AiResult_Failure);
    check_eq_float(ai_blackboard_get_f64(bb, string_hash_lit("test")), 42.42, 1e-6f);
  }

  teardown() { ai_blackboard_destroy(bb); }
}
