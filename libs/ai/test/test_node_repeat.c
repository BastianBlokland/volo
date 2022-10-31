#include "ai_eval.h"
#include "ai_tracer_count.h"
#include "asset_behavior.h"
#include "check_spec.h"
#include "core_alloc.h"
#include "script_mem.h"

spec(node_repeat) {
  ScriptMem*    memory = null;
  AiTracerCount tracer;

  setup() {
    memory = script_mem_create(g_alloc_heap);
    tracer = ai_tracer_count();
  }

  it("evaluates to running when child evaluates to running") {
    const AssetAiNode nodeDefs[] = {
        {
            .type        = AssetAiNode_Repeat,
            .nextSibling = sentinel_u16,
            .data_repeat = {.child = 1},
        },
        {.type = AssetAiNode_Running, .nextSibling = sentinel_u16},
    };
    const AiEvalContext ctx = {
        .memory   = memory,
        .tracer   = &tracer.api,
        .nodeDefs = nodeDefs,
    };
    check(ai_eval(&ctx, AssetAiNodeRoot) == AiResult_Running);
    check_eq_int(tracer.count, 2);
  }

  it("evaluates to running when child evaluates to success") {
    const AssetAiNode nodeDefs[] = {
        {
            .type        = AssetAiNode_Repeat,
            .nextSibling = sentinel_u16,
            .data_repeat = {.child = 1},
        },
        {.type = AssetAiNode_Success, .nextSibling = sentinel_u16},
    };
    const AiEvalContext ctx = {
        .memory   = memory,
        .tracer   = &tracer.api,
        .nodeDefs = nodeDefs,
    };
    check(ai_eval(&ctx, AssetAiNodeRoot) == AiResult_Running);
    check_eq_int(tracer.count, 2);
  }

  it("evaluates to failure when child evaluates to failure") {
    const AssetAiNode nodeDefs[] = {
        {
            .type        = AssetAiNode_Repeat,
            .nextSibling = sentinel_u16,
            .data_repeat = {.child = 1},
        },
        {.type = AssetAiNode_Failure, .nextSibling = sentinel_u16},
    };
    const AiEvalContext ctx = {
        .memory   = memory,
        .tracer   = &tracer.api,
        .nodeDefs = nodeDefs,
    };
    check(ai_eval(&ctx, AssetAiNodeRoot) == AiResult_Failure);
    check_eq_int(tracer.count, 2);
  }

  it("always evaluates the child node") {
    const AssetAiNode nodeDefs[] = {
        {
            .type        = AssetAiNode_Repeat,
            .nextSibling = sentinel_u16,
            .data_repeat = {.child = 1},
        },
        {
            .type        = AssetAiNode_KnowledgeSet,
            .nextSibling = sentinel_u16,
            .data_knowledgeset =
                {
                    .key   = string_hash_lit("test"),
                    .value = {.type = AssetAiSource_Number, .data_number.value = 42.42},
                },
        },
    };
    const AiEvalContext ctx = {
        .memory   = memory,
        .tracer   = &tracer.api,
        .nodeDefs = nodeDefs,
    };
    check(ai_eval(&ctx, AssetAiNodeRoot) == AiResult_Running);
    check_eq_int(tracer.count, 2);
    check(script_val_equal(script_mem_get(memory, string_hash_lit("test")), script_number(42.42)));
  }

  teardown() { script_mem_destroy(memory); }
}
