#include "ai_eval.h"
#include "ai_tracer_count.h"
#include "asset_behavior.h"
#include "check_spec.h"
#include "core_alloc.h"
#include "script_doc.h"
#include "script_mem.h"

spec(node_condition) {
  ScriptMem*    memory    = null;
  ScriptDoc*    scriptDoc = null;
  AiTracerCount tracer;

  setup() {
    memory    = script_mem_create(g_alloc_heap);
    scriptDoc = script_create(g_alloc_heap);
    tracer    = ai_tracer_count();
  }

  it("evaluates to success when the condition is truthy") {
    const AssetAiNode nodeDefs[] = {
        {
            .type           = AssetAiNode_Condition,
            .nextSibling    = sentinel_u16,
            .data_condition = {.scriptExpr = script_add_value(scriptDoc, script_bool(true))},
        },
    };
    const AiEvalContext ctx = {
        .memory    = memory,
        .tracer    = &tracer.api,
        .nodeDefs  = nodeDefs,
        .scriptDoc = scriptDoc,
    };
    check(ai_eval(&ctx, AssetAiNodeRoot) == AiResult_Success);
    check_eq_int(tracer.count, 1);
  }

  it("evaluates to failure when the condition is falsy") {
    const AssetAiNode nodeDefs[] = {
        {
            .type           = AssetAiNode_Condition,
            .nextSibling    = sentinel_u16,
            .data_condition = {.scriptExpr = script_add_value(scriptDoc, script_bool(false))},
        },
    };
    const AiEvalContext ctx = {
        .memory    = memory,
        .tracer    = &tracer.api,
        .nodeDefs  = nodeDefs,
        .scriptDoc = scriptDoc,
    };
    check(ai_eval(&ctx, AssetAiNodeRoot) == AiResult_Failure);
    check_eq_int(tracer.count, 1);
  }

  teardown() {
    script_mem_destroy(memory);
    script_destroy(scriptDoc);
  }
}
