#include "ai.h"
#include "asset_behavior.h"
#include "check_spec.h"
#include "core_alloc.h"
#include "core_array.h"

spec(node_sequence) {
  AiBlackboard* bb = null;

  setup() { bb = ai_blackboard_create(g_alloc_heap); }

  it("evaluates to success when it doesn't have any children") {
    const AssetBehavior behavior = {
        .type          = AssetBehaviorType_Sequence,
        .data_sequence = {.children = {0}},
    };
    check(ai_eval(&behavior, bb) == AiResult_Success);
  }

  it("evaluates to success when all children evaluate to success") {
    const AssetBehavior children[] = {
        {.type = AssetBehaviorType_Success},
        {.type = AssetBehaviorType_Success},
        {.type = AssetBehaviorType_Success},
    };
    const AssetBehavior behavior = {
        .type          = AssetBehaviorType_Sequence,
        .data_sequence = {.children = {.values = children, array_elems(children)}},
    };
    check(ai_eval(&behavior, bb) == AiResult_Success);
  }

  it("evaluates to failure when any child evaluates to failure") {
    const AssetBehavior children[] = {
        {.type = AssetBehaviorType_Success},
        {.type = AssetBehaviorType_Failure},
        {.type = AssetBehaviorType_Success},
    };
    const AssetBehavior behavior = {
        .type          = AssetBehaviorType_Sequence,
        .data_sequence = {.children = {.values = children, array_elems(children)}},
    };
    check(ai_eval(&behavior, bb) == AiResult_Failure);
  }

  teardown() { ai_blackboard_destroy(bb); }
}
