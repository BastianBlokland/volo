#include "ai_eval.h"
#include "ai_tracer_count.h"
#include "asset_behavior.h"
#include "check_spec.h"
#include "core_alloc.h"
#include "script_mem.h"

spec(node_running) {
  ScriptMem*    memory = null;
  AiTracerCount tracer;

  setup() {
    memory = script_mem_create(g_alloc_heap);
    tracer = ai_tracer_count();
  }

  it("evaluates to running") {
    const AssetAiNode nodeDefs[] = {
        {.type = AssetAiNode_Running, .nextSibling = sentinel_u16},
    };
    const AiEvalContext ctx = {
        .memory   = memory,
        .tracer   = &tracer.api,
        .nodeDefs = nodeDefs,
    };
    check(ai_eval(&ctx, AssetAiNodeRoot) == AiResult_Running);
    check_eq_int(tracer.count, 1);
  }

  teardown() { script_mem_destroy(memory); }
}
