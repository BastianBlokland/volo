#include "ai.h"
#include "ai_tracer_count.h"
#include "asset_behavior.h"
#include "check_spec.h"
#include "core_alloc.h"

spec(node_success) {
  AiBlackboard* bb = null;
  AiTracerCount tracer;

  setup() {
    bb     = ai_blackboard_create(g_alloc_heap);
    tracer = ai_tracer_count();
  }

  it("evaluates to success") {
    const AssetAiNode nodeDefs[] = {
        {.type = AssetAiNode_Success, .nextSibling = sentinel_u16},
    };
    const AiEvalContext ctx = {
        .memory   = bb,
        .tracer   = &tracer.api,
        .nodeDefs = nodeDefs,
    };
    check(ai_eval(&ctx, AssetAiNodeRoot) == AiResult_Success);
    check_eq_int(tracer.count, 1);
  }

  teardown() { ai_blackboard_destroy(bb); }
}
