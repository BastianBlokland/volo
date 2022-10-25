#include "ai.h"
#include "ai_tracer_count.h"
#include "asset_behavior.h"
#include "check_spec.h"
#include "core_alloc.h"
#include "core_array.h"

#include "utils_internal.h"

spec(node_parallel) {
  AiBlackboard* bb = null;
  AiTracerCount tracer;

  setup() {
    bb     = ai_blackboard_create(g_alloc_heap);
    tracer = ai_tracer_count();
  }

  it("evaluates to failure when it doesn't have any children") {
    const AssetBehavior behavior = {
        .type          = AssetBehavior_Parallel,
        .data_parallel = {.children = {0}},
    };
    check(ai_eval(&behavior, bb, &tracer.api) == AiResult_Failure);
    check_eq_int(tracer.count, 1);
  }

  it("evaluates to success when any child evaluates to success") {
    const AssetBehavior children[] = {
        {.type = AssetBehavior_Failure},
        {.type = AssetBehavior_Success},
        {.type = AssetBehavior_Running},
        {.type = AssetBehavior_Failure},
    };
    const AssetBehavior behavior = {
        .type          = AssetBehavior_Parallel,
        .data_parallel = {.children = {.values = children, array_elems(children)}},
    };
    check(ai_eval(&behavior, bb, &tracer.api) == AiResult_Success);
    check_eq_int(tracer.count, 5);
  }

  it("evaluates to running when any child evaluates to running") {
    const AssetBehavior children[] = {
        {.type = AssetBehavior_Failure},
        {.type = AssetBehavior_Running},
        {.type = AssetBehavior_Failure},
    };
    const AssetBehavior behavior = {
        .type          = AssetBehavior_Parallel,
        .data_parallel = {.children = {.values = children, array_elems(children)}},
    };
    check(ai_eval(&behavior, bb, &tracer.api) == AiResult_Running);
    check_eq_int(tracer.count, 4);
  }

  it("evaluates to failure when all children evaluate to failure") {
    const AssetBehavior children[] = {
        {.type = AssetBehavior_Failure},
        {.type = AssetBehavior_Failure},
        {.type = AssetBehavior_Failure},
    };
    const AssetBehavior behavior = {
        .type          = AssetBehavior_Parallel,
        .data_parallel = {.children = {.values = children, array_elems(children)}},
    };
    check(ai_eval(&behavior, bb, &tracer.api) == AiResult_Failure);
    check_eq_int(tracer.count, 4);
  }

  it("evaluates all the child nodes") {
    const AssetBehavior children[] = {
        {
            .type = AssetBehavior_KnowledgeSet,
            .data_knowledgeset =
                {
                    .key   = string_lit("test1"),
                    .value = {.type = AssetAiSource_Number, .data_number.value = 1},
                },
        },
        {
            .type = AssetBehavior_KnowledgeSet,
            .data_knowledgeset =
                {
                    .key   = string_lit("test2"),
                    .value = {.type = AssetAiSource_Number, .data_number.value = 2},
                },
        },
        {
            .type = AssetBehavior_KnowledgeSet,
            .data_knowledgeset =
                {
                    .key   = string_lit("test3"),
                    .value = {.type = AssetAiSource_Number, .data_number.value = 3},
                },
        },
    };
    const AssetBehavior behavior = {
        .type          = AssetBehavior_Parallel,
        .data_parallel = {.children = {.values = children, array_elems(children)}},
    };
    check(ai_eval(&behavior, bb, &tracer.api) == AiResult_Success);
    check_eq_int(tracer.count, 4);
    check_eq_value(ai_blackboard_get(bb, string_hash_lit("test1")), ai_value_f64(1));
    check_eq_value(ai_blackboard_get(bb, string_hash_lit("test2")), ai_value_f64(2));
    check_eq_value(ai_blackboard_get(bb, string_hash_lit("test3")), ai_value_f64(3));
  }

  teardown() { ai_blackboard_destroy(bb); }
}
