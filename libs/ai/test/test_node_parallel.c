#include "ai.h"
#include "ai_tracer_count.h"
#include "asset_behavior.h"
#include "check_spec.h"
#include "core_alloc.h"
#include "core_array.h"

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
        {.type = AssetBehavior_Failure},
    };
    const AssetBehavior behavior = {
        .type          = AssetBehavior_Parallel,
        .data_parallel = {.children = {.values = children, array_elems(children)}},
    };
    check(ai_eval(&behavior, bb, &tracer.api) == AiResult_Success);
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
                    .value = {.type = AssetKnowledgeSource_Number, .data_number.value = 1},
                },
        },
        {
            .type = AssetBehavior_KnowledgeSet,
            .data_knowledgeset =
                {
                    .key   = string_lit("test2"),
                    .value = {.type = AssetKnowledgeSource_Number, .data_number.value = 2},
                },
        },
        {
            .type = AssetBehavior_KnowledgeSet,
            .data_knowledgeset =
                {
                    .key   = string_lit("test3"),
                    .value = {.type = AssetKnowledgeSource_Number, .data_number.value = 3},
                },
        },
    };
    const AssetBehavior behavior = {
        .type          = AssetBehavior_Parallel,
        .data_parallel = {.children = {.values = children, array_elems(children)}},
    };
    check(ai_eval(&behavior, bb, &tracer.api) == AiResult_Success);
    check_eq_int(tracer.count, 4);
    check_eq_float(ai_blackboard_get_f64(bb, string_hash_lit("test1")), 1, 1e-6f);
    check_eq_float(ai_blackboard_get_f64(bb, string_hash_lit("test2")), 2, 1e-6f);
    check_eq_float(ai_blackboard_get_f64(bb, string_hash_lit("test3")), 3, 1e-6f);
  }

  teardown() { ai_blackboard_destroy(bb); }
}
