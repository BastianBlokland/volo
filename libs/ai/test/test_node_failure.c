#include "ai.h"
#include "asset_behavior.h"
#include "check_spec.h"
#include "core_alloc.h"

spec(node_failure) {
  AiBlackboard* bb = null;

  setup() { bb = ai_blackboard_create(g_alloc_heap); }

  it("evaluates to failure") {
    const AssetBehavior behavior = {.type = AssetBehavior_Failure};
    check(ai_eval(&behavior, bb, null) == AiResult_Failure);
  }

  teardown() { ai_blackboard_destroy(bb); }
}
