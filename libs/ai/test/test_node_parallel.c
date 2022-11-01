#include "ai_eval.h"
#include "ai_tracer_count.h"
#include "asset_behavior.h"
#include "check_spec.h"
#include "core_alloc.h"
#include "core_array.h"
#include "script_doc.h"
#include "script_mem.h"

spec(node_parallel) {
  ScriptMem*    memory    = null;
  ScriptDoc*    scriptDoc = null;
  AiTracerCount tracer;

  setup() {
    memory    = script_mem_create(g_alloc_heap);
    scriptDoc = script_create(g_alloc_heap);
    tracer    = ai_tracer_count();
  }

  it("evaluates to failure when it doesn't have any children") {
    const AssetAiNode nodeDefs[] = {
        {
            .type          = AssetAiNode_Parallel,
            .nextSibling   = sentinel_u16,
            .data_parallel = {.childrenBegin = sentinel_u16},
        },
    };
    const AiEvalContext ctx = {
        .memory   = memory,
        .tracer   = &tracer.api,
        .nodeDefs = nodeDefs,
    };
    check(ai_eval(&ctx, AssetAiNodeRoot) == AiResult_Failure);
    check_eq_int(tracer.count, 1);
  }

  it("evaluates to success when any child evaluates to success") {
    const AssetAiNode nodeDefs[] = {
        {
            .type          = AssetAiNode_Parallel,
            .nextSibling   = sentinel_u16,
            .data_parallel = {.childrenBegin = 1},
        },
        {.type = AssetAiNode_Failure, .nextSibling = 2},
        {.type = AssetAiNode_Success, .nextSibling = 3},
        {.type = AssetAiNode_Running, .nextSibling = 4},
        {.type = AssetAiNode_Failure, .nextSibling = sentinel_u16},
    };
    const AiEvalContext ctx = {
        .memory   = memory,
        .tracer   = &tracer.api,
        .nodeDefs = nodeDefs,
    };
    check(ai_eval(&ctx, AssetAiNodeRoot) == AiResult_Success);
    check_eq_int(tracer.count, 5);
  }

  it("evaluates to running when any child evaluates to running") {
    const AssetAiNode nodeDefs[] = {
        {
            .type          = AssetAiNode_Parallel,
            .nextSibling   = sentinel_u16,
            .data_parallel = {.childrenBegin = 1},
        },
        {.type = AssetAiNode_Failure, .nextSibling = 2},
        {.type = AssetAiNode_Running, .nextSibling = 3},
        {.type = AssetAiNode_Failure, .nextSibling = sentinel_u16},
    };
    const AiEvalContext ctx = {
        .memory   = memory,
        .tracer   = &tracer.api,
        .nodeDefs = nodeDefs,
    };
    check(ai_eval(&ctx, AssetAiNodeRoot) == AiResult_Running);
    check_eq_int(tracer.count, 4);
  }

  it("evaluates to failure when all children evaluate to failure") {
    const AssetAiNode nodeDefs[] = {
        {
            .type          = AssetAiNode_Parallel,
            .nextSibling   = sentinel_u16,
            .data_parallel = {.childrenBegin = 1},
        },
        {.type = AssetAiNode_Failure, .nextSibling = 2},
        {.type = AssetAiNode_Failure, .nextSibling = 3},
        {.type = AssetAiNode_Failure, .nextSibling = sentinel_u16},
    };
    const AiEvalContext ctx = {
        .memory   = memory,
        .tracer   = &tracer.api,
        .nodeDefs = nodeDefs,
    };
    check(ai_eval(&ctx, AssetAiNodeRoot) == AiResult_Failure);
    check_eq_int(tracer.count, 4);
  }

  it("evaluates all the child nodes") {
    const AssetAiNode nodeDefs[] = {
        {
            .type          = AssetAiNode_Parallel,
            .nextSibling   = sentinel_u16,
            .data_parallel = {.childrenBegin = 1},
        },
        {
            .type        = AssetAiNode_Execute,
            .nextSibling = 2,
            .data_execute =
                {
                    .scriptExpr = script_add_store(
                        scriptDoc,
                        string_hash_lit("test1"),
                        script_add_value(scriptDoc, script_number(1))),
                },
        },
        {
            .type        = AssetAiNode_Execute,
            .nextSibling = 3,
            .data_execute =
                {
                    .scriptExpr = script_add_store(
                        scriptDoc,
                        string_hash_lit("test2"),
                        script_add_value(scriptDoc, script_number(2))),
                },
        },
        {
            .type        = AssetAiNode_Execute,
            .nextSibling = sentinel_u16,
            .data_execute =
                {
                    .scriptExpr = script_add_store(
                        scriptDoc,
                        string_hash_lit("test3"),
                        script_add_value(scriptDoc, script_number(3))),
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
    check_eq_int(tracer.count, 4);
    check(script_val_equal(script_mem_get(memory, string_hash_lit("test1")), script_number(1)));
    check(script_val_equal(script_mem_get(memory, string_hash_lit("test2")), script_number(2)));
    check(script_val_equal(script_mem_get(memory, string_hash_lit("test3")), script_number(3)));
  }

  teardown() {
    script_mem_destroy(memory);
    script_destroy(scriptDoc);
  }
}
