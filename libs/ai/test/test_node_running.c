#include "ai.h"
#include "ai_tracer_count.h"
#include "asset_behavior.h"
#include "check_spec.h"
#include "core_alloc.h"

spec(node_running) {
  AiBlackboard* bb = null;
  AiTracerCount tracer;

  setup() {
    bb     = ai_blackboard_create(g_alloc_heap);
    tracer = ai_tracer_count();
  }

  it("evaluates to running") {
    const AssetBehavior behavior = {.type = AssetBehavior_Running};
    check(ai_eval(&behavior, bb, &tracer.api) == AiResult_Running);
    check_eq_int(tracer.count, 1);
  }

  teardown() { ai_blackboard_destroy(bb); }
}
