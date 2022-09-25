#include "ai.h"
#include "ai_tracer_count.h"
#include "asset_behavior.h"
#include "check_spec.h"
#include "core_alloc.h"
#include "core_array.h"

spec(node_selector) {
  AiBlackboard* bb = null;
  AiTracerCount tracer;

  setup() {
    bb     = ai_blackboard_create(g_alloc_heap);
    tracer = ai_tracer_count();
  }

  it("evaluates to failure when it doesn't have any children") {
    const AssetBehavior behavior = {
        .type          = AssetBehavior_Selector,
        .data_selector = {.children = {0}},
    };
    check(ai_eval(&behavior, bb, &tracer.api) == AiResult_Failure);
    check_eq_int(tracer.count, 1);
  }

  it("evaluates to success when any child evaluates to success") {
    const AssetBehavior children[] = {
        {.type = AssetBehavior_Failure},
        {.type = AssetBehavior_Success},
        {.type = AssetBehavior_Failure},
    };
    const AssetBehavior behavior = {
        .type          = AssetBehavior_Selector,
        .data_selector = {.children = {.values = children, array_elems(children)}},
    };
    check(ai_eval(&behavior, bb, &tracer.api) == AiResult_Success);
    check_eq_int(tracer.count, 3);
  }

  it("evaluates to failure when all children evaluate to failure") {
    const AssetBehavior children[] = {
        {.type = AssetBehavior_Failure},
        {.type = AssetBehavior_Failure},
        {.type = AssetBehavior_Failure},
    };
    const AssetBehavior behavior = {
        .type          = AssetBehavior_Selector,
        .data_selector = {.children = {.values = children, array_elems(children)}},
    };
    check(ai_eval(&behavior, bb, &tracer.api) == AiResult_Failure);
    check_eq_int(tracer.count, 4);
  }

  teardown() { ai_blackboard_destroy(bb); }
}
