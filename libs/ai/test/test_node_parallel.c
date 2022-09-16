#include "ai.h"
#include "asset_behavior.h"
#include "check_spec.h"
#include "core_alloc.h"
#include "core_array.h"

spec(node_parallel) {
  AiBlackboard* bb = null;

  setup() { bb = ai_blackboard_create(g_alloc_heap); }

  it("evaluates to failure when it doesn't have any children") {
    const AssetBehavior behavior = {
        .type          = AssetBehaviorType_Parallel,
        .data_parallel = {.children = {0}},
    };
    check(ai_eval(&behavior, bb) == AiResult_Failure);
  }

  it("evaluates to success when any child evaluates to success") {
    const AssetBehavior children[] = {
        {.type = AssetBehaviorType_Failure},
        {.type = AssetBehaviorType_Success},
        {.type = AssetBehaviorType_Failure},
    };
    const AssetBehavior behavior = {
        .type          = AssetBehaviorType_Parallel,
        .data_parallel = {.children = {.values = children, array_elems(children)}},
    };
    check(ai_eval(&behavior, bb) == AiResult_Success);
  }

  it("evaluates to failure when all children evaluate to failure") {
    const AssetBehavior children[] = {
        {.type = AssetBehaviorType_Failure},
        {.type = AssetBehaviorType_Failure},
        {.type = AssetBehaviorType_Failure},
    };
    const AssetBehavior behavior = {
        .type          = AssetBehaviorType_Parallel,
        .data_parallel = {.children = {.values = children, array_elems(children)}},
    };
    check(ai_eval(&behavior, bb) == AiResult_Failure);
  }

  it("evaluates all the child nodes") {
    const AssetBehavior children[] = {
        {
            .type = AssetBehaviorType_KnowledgeSet,
            .data_knowledgeset =
                {
                    .key   = string_lit("test1"),
                    .value = {.type = AssetKnowledgeSource_f64, .data_f64 = 1},
                },
        },
        {
            .type = AssetBehaviorType_KnowledgeSet,
            .data_knowledgeset =
                {
                    .key   = string_lit("test2"),
                    .value = {.type = AssetKnowledgeSource_f64, .data_f64 = 2},
                },
        },
        {
            .type = AssetBehaviorType_KnowledgeSet,
            .data_knowledgeset =
                {
                    .key   = string_lit("test3"),
                    .value = {.type = AssetKnowledgeSource_f64, .data_f64 = 3},
                },
        },
    };
    const AssetBehavior behavior = {
        .type          = AssetBehaviorType_Parallel,
        .data_parallel = {.children = {.values = children, array_elems(children)}},
    };
    check(ai_eval(&behavior, bb) == AiResult_Success);
    check_eq_float(ai_blackboard_get_f64(bb, string_hash_lit("test1")), 1, 1e-6f);
    check_eq_float(ai_blackboard_get_f64(bb, string_hash_lit("test2")), 2, 1e-6f);
    check_eq_float(ai_blackboard_get_f64(bb, string_hash_lit("test3")), 3, 1e-6f);
  }

  teardown() { ai_blackboard_destroy(bb); }
}
