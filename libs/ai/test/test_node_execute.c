#include "ai_eval.h"
#include "ai_tracer_count.h"
#include "asset_behavior.h"
#include "check_spec.h"
#include "core_alloc.h"
#include "script_doc.h"
#include "script_mem.h"

spec(node_execute) {
  ScriptMem*    memory    = null;
  ScriptDoc*    scriptDoc = null;
  AiTracerCount tracer;

  setup() {
    memory    = script_mem_create(g_alloc_heap);
    scriptDoc = script_create(g_alloc_heap);
    tracer    = ai_tracer_count();
  }

  it("evaluates to success and updates memory") {
    const StringHash key = string_hash_lit("hello_world");

    const AssetAiNode nodeDefs[] = {
        {
            .type        = AssetAiNode_Execute,
            .nextSibling = sentinel_u16,
            .data_execute =
                {
                    .scriptExpr = script_add_store(
                        scriptDoc, key, script_add_value(scriptDoc, script_number(42))),
                },
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
    check_eq_int(script_get_number(script_mem_get(memory, key), 0), 42);
  }

  teardown() {
    script_mem_destroy(memory);
    script_destroy(scriptDoc);
  }
}
