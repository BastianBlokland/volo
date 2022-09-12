#include "ai.h"
#include "ai_eval.h"
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

  teardown() { ai_blackboard_destroy(bb); }
}
