#include "ai.h"
#include "asset_behavior.h"
#include "check_spec.h"
#include "core_alloc.h"

spec(node_knowledgecompare) {
  AiBlackboard* bb = null;

  setup() { bb = ai_blackboard_create(g_alloc_heap); }

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
    check(ai_eval(&behavior, bb, null) == AiResult_Success);
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
    check(ai_eval(&behavior, bb, null) == AiResult_Failure);
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
    check(ai_eval(&behavior, bb, null) == AiResult_Failure);
  }

  teardown() { ai_blackboard_destroy(bb); }
}
