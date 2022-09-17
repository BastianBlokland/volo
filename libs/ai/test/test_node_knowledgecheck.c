#include "ai.h"
#include "asset_behavior.h"
#include "check_spec.h"
#include "core_alloc.h"

spec(node_knowledgecheck) {
  AiBlackboard* bb = null;

  setup() { bb = ai_blackboard_create(g_alloc_heap); }

  it("evaluates to failure when knowledge for the key does not exist") {
    const AssetBehavior behavior = {
        .type                = AssetBehavior_KnowledgeCheck,
        .data_knowledgecheck = {.key = string_lit("test")},
    };
    check(ai_eval(&behavior, bb) == AiResult_Failure);
  }

  it("evaluates to success when knowledge for the key exists") {
    ai_blackboard_set_f64(bb, string_hash_lit("test"), 42);

    const AssetBehavior behavior = {
        .type                = AssetBehavior_KnowledgeCheck,
        .data_knowledgecheck = {.key = string_lit("test")},
    };
    check(ai_eval(&behavior, bb) == AiResult_Success);
  }

  teardown() { ai_blackboard_destroy(bb); }
}
