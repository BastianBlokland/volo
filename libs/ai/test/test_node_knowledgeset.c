#include "ai_eval.h"
#include "ai_tracer_count.h"
#include "asset_behavior.h"
#include "check_spec.h"
#include "core_alloc.h"
#include "core_time.h"
#include "script_mem.h"

spec(node_knowledgeset) {
  ScriptMem*    memory = null;
  AiTracerCount tracer;

  setup() {
    memory = script_mem_create(g_alloc_heap);
    tracer = ai_tracer_count();
  }

  it("can set f64 knowledge when evaluated") {
    check(script_val_equal(script_mem_get(memory, string_hash_lit("test")), script_null()));

    const AssetAiNode nodeDefs[] = {
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
    check(ai_eval(&ctx, AssetAiNodeRoot) == AiResult_Success);
    check_eq_int(tracer.count, 1);
    check(script_val_equal(script_mem_get(memory, string_hash_lit("test")), script_number(42.42)));
  }

  it("can set boolean knowledge when evaluated") {
    check(script_val_equal(script_mem_get(memory, string_hash_lit("test")), script_null()));

    const AssetAiNode nodeDefs[] = {
        {
            .type        = AssetAiNode_KnowledgeSet,
            .nextSibling = sentinel_u16,
            .data_knowledgeset =
                {
                    .key   = string_hash_lit("test"),
                    .value = {.type = AssetAiSource_Bool, .data_bool.value = true},
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
    check(script_val_equal(script_mem_get(memory, string_hash_lit("test")), script_bool(true)));
  }

  it("can set vector knowledge when evaluated") {
    check(script_val_equal(script_mem_get(memory, string_hash_lit("test")), script_null()));

    const AssetAiNode nodeDefs[] = {
        {
            .type        = AssetAiNode_KnowledgeSet,
            .nextSibling = sentinel_u16,
            .data_knowledgeset =
                {
                    .key = string_hash_lit("test"),
                    .value =
                        {.type = AssetAiSource_Vector, .data_vector = {.x = 1, .y = 2, .z = 3}},
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
    check(script_val_equal(
        script_mem_get(memory, string_hash_lit("test")), script_vector3(geo_vector(1, 2, 3))));
  }

  it("can set time knowledge when evaluated") {
    check(script_val_equal(script_mem_get(memory, string_hash_lit("test")), script_null()));

    const AssetAiNode nodeDefs[] = {
        {
            .type        = AssetAiNode_KnowledgeSet,
            .nextSibling = sentinel_u16,
            .data_knowledgeset =
                {
                    .key   = string_hash_lit("test"),
                    .value = {.type = AssetAiSource_Time, .data_time.secondsFromNow = 1.75f},
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
    check(script_val_equal(
        script_mem_get(memory, string_hash_lit("test")),
        script_time(time_second + time_milliseconds(750))));
  }

  it("can set knowledge based on other knowledge when evaluated") {
    script_mem_set(memory, string_hash_lit("test1"), script_number(42));

    const AssetAiNode nodeDefs[] = {
        {
            .type        = AssetAiNode_KnowledgeSet,
            .nextSibling = sentinel_u16,
            .data_knowledgeset =
                {
                    .key = string_hash_lit("test2"),
                    .value =
                        {
                            .type           = AssetAiSource_Knowledge,
                            .data_knowledge = string_hash_lit("test1"),
                        },
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
    check(script_val_equal(script_mem_get(memory, string_hash_lit("test2")), script_number(42)));
  }

  teardown() { script_mem_destroy(memory); }
}
