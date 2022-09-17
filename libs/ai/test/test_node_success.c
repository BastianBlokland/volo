#include "ai.h"
#include "asset_behavior.h"
#include "check_spec.h"
#include "core_alloc.h"

spec(node_success) {
  AiBlackboard* bb = null;

  setup() { bb = ai_blackboard_create(g_alloc_heap); }

  it("evaluates to success") {
    const AssetBehavior behavior = {.type = AssetBehavior_Success};
    check(ai_eval(&behavior, bb) == AiResult_Success);
  }

  teardown() { ai_blackboard_destroy(bb); }
}
