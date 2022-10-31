#include "ai_eval.h"
#include "ai_tracer_count.h"
#include "asset_behavior.h"
#include "check_spec.h"
#include "core_alloc.h"
#include "script_mem.h"

spec(node_knowledgecompare) {
  ScriptMem*    memory = null;
  AiTracerCount tracer;

  setup() {
    memory = script_mem_create(g_alloc_heap);
    tracer = ai_tracer_count();
  }

  it("evaluates to success when equals comparison succeeds") {
    script_mem_set(memory, string_hash_lit("test"), script_bool(true));

    const AssetAiNode nodeDefs[] = {
        {
            .type        = AssetAiNode_KnowledgeCompare,
            .nextSibling = sentinel_u16,
            .data_knowledgecompare =
                {
                    .comparison = AssetAiComparison_Equal,
                    .key        = string_hash_lit("test"),
                    .value      = {.type = AssetAiSource_Bool, .data_bool = true},
                },
        },
    };
    const AiEvalContext ctx = {
        .memory   = memory,
        .tracer   = &tracer.api,
        .nodeDefs = nodeDefs,
    };
    check(ai_eval(&ctx, AssetAiNodeRoot) == AiResult_Success);
    check_eq_int(tracer.count, 1);
  }

  it("evaluates to failure when the key does not exist") {
    const AssetAiNode nodeDefs[] = {
        {
            .type        = AssetAiNode_KnowledgeCompare,
            .nextSibling = sentinel_u16,
            .data_knowledgecompare =
                {
                    .comparison = AssetAiComparison_Equal,
                    .key        = string_hash_lit("test"),
                    .value      = {.type = AssetAiSource_Bool, .data_bool = true},
                },
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

  it("evaluates to failure when equals comparison fails") {
    script_mem_set(memory, string_hash_lit("test"), script_bool(false));

    const AssetAiNode nodeDefs[] = {
        {
            .type        = AssetAiNode_KnowledgeCompare,
            .nextSibling = sentinel_u16,
            .data_knowledgecompare =
                {
                    .comparison = AssetAiComparison_Equal,
                    .key        = string_hash_lit("test"),
                    .value      = {.type = AssetAiSource_Bool, .data_bool = true},
                },
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

  it("evaluates to success when less comparison succeeds") {
    script_mem_set(memory, string_hash_lit("test"), script_number(42));

    const AssetAiNode nodeDefs[] = {
        {
            .type        = AssetAiNode_KnowledgeCompare,
            .nextSibling = sentinel_u16,
            .data_knowledgecompare =
                {
                    .comparison = AssetAiComparison_Less,
                    .key        = string_hash_lit("test"),
                    .value      = {.type = AssetAiSource_Number, .data_number.value = 1337},
                },
        },
    };
    const AiEvalContext ctx = {
        .memory   = memory,
        .tracer   = &tracer.api,
        .nodeDefs = nodeDefs,
    };
    check(ai_eval(&ctx, AssetAiNodeRoot) == AiResult_Success);
    check_eq_int(tracer.count, 1);
  }

  it("evaluates to failure when less comparison fails") {
    script_mem_set(memory, string_hash_lit("test"), script_number(42));
    script_mem_set(memory, string_hash_lit("value"), script_number(10));

    const AssetAiNode nodeDefs[] = {
        {
            .type        = AssetAiNode_KnowledgeCompare,
            .nextSibling = sentinel_u16,
            .data_knowledgecompare =
                {
                    .comparison = AssetAiComparison_Less,
                    .key        = string_hash_lit("test"),
                    .value =
                        {
                            .type               = AssetAiSource_Knowledge,
                            .data_knowledge.key = string_hash_lit("value"),
                        },
                },
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

  teardown() { script_mem_destroy(memory); }
}
