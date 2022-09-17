#include "ai.h"
#include "asset_behavior.h"
#include "check_spec.h"
#include "core_alloc.h"

spec(node_knowledgeclear) {
  AiBlackboard* bb = null;

  setup() { bb = ai_blackboard_create(g_alloc_heap); }

  it("unset's knowledge when evaluated") {
    ai_blackboard_set_f64(bb, string_hash_lit("test"), 42);
    check_eq_float(ai_blackboard_get_f64(bb, string_hash_lit("test")), 42, 1e-6f);

    const AssetBehavior behavior = {
        .type                = AssetBehavior_KnowledgeClear,
        .data_knowledgeclear = {.key = string_lit("test")},
    };
    check(ai_eval(&behavior, bb) == AiResult_Success);
    check_eq_float(ai_blackboard_get_f64(bb, string_hash_lit("test")), 0, 1e-6f);
  }

  teardown() { ai_blackboard_destroy(bb); }
}
