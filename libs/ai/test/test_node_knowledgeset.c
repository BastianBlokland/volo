#include "ai.h"
#include "asset_behavior.h"
#include "check_spec.h"
#include "core_alloc.h"

spec(node_knowledgeset) {
  AiBlackboard* bb = null;

  setup() { bb = ai_blackboard_create(g_alloc_heap); }

  it("can set f64 knowledge when evaluated") {
    check_eq_float(ai_blackboard_get_f64(bb, string_hash_lit("test")), 0, 1e-6f);

    const AssetBehavior behavior = {
        .type = AssetBehavior_KnowledgeSet,
        .data_knowledgeset =
            {
                .key   = string_lit("test"),
                .value = {.type = AssetKnowledgeSource_Number, .data_number.value = 42.42},
            },
    };
    check(ai_eval(&behavior, bb, null) == AiResult_Success);
    check_eq_float(ai_blackboard_get_f64(bb, string_hash_lit("test")), 42.42, 1e-6f);
  }

  it("can set boolean knowledge when evaluated") {
    check(!ai_blackboard_get_bool(bb, string_hash_lit("test")));

    const AssetBehavior behavior = {
        .type = AssetBehavior_KnowledgeSet,
        .data_knowledgeset =
            {
                .key   = string_lit("test"),
                .value = {.type = AssetKnowledgeSource_Bool, .data_bool.value = true},
            },
    };
    check(ai_eval(&behavior, bb, null) == AiResult_Success);
    check(ai_blackboard_get_bool(bb, string_hash_lit("test")));
  }

  it("can set vector knowledge when evaluated") {
    check_eq_float(ai_blackboard_get_vector(bb, string_hash_lit("test")).x, 0, 1e-6f);
    check_eq_float(ai_blackboard_get_vector(bb, string_hash_lit("test")).y, 0, 1e-6f);
    check_eq_float(ai_blackboard_get_vector(bb, string_hash_lit("test")).z, 0, 1e-6f);
    check_eq_float(ai_blackboard_get_vector(bb, string_hash_lit("test")).w, 0, 1e-6f);

    const AssetBehavior behavior = {
        .type = AssetBehavior_KnowledgeSet,
        .data_knowledgeset =
            {
                .key = string_lit("test"),
                .value =
                    {
                        .type        = AssetKnowledgeSource_Vector,
                        .data_vector = {.x = 1, .y = 2, .z = 3, .w = 4},
                    },
            },
    };
    check(ai_eval(&behavior, bb, null) == AiResult_Success);
    check_eq_float(ai_blackboard_get_vector(bb, string_hash_lit("test")).x, 1, 1e-6f);
    check_eq_float(ai_blackboard_get_vector(bb, string_hash_lit("test")).y, 2, 1e-6f);
    check_eq_float(ai_blackboard_get_vector(bb, string_hash_lit("test")).z, 3, 1e-6f);
    check_eq_float(ai_blackboard_get_vector(bb, string_hash_lit("test")).w, 4, 1e-6f);
  }

  it("can set knowledge based on other knowledge when evaluated") {
    ai_blackboard_set_f64(bb, string_hash_lit("test1"), 42);

    const AssetBehavior behavior = {
        .type = AssetBehavior_KnowledgeSet,
        .data_knowledgeset =
            {
                .key = string_lit("test2"),
                .value =
                    {
                        .type           = AssetKnowledgeSource_Knowledge,
                        .data_knowledge = string_lit("test1"),
                    },
            },
    };
    check(ai_eval(&behavior, bb, null) == AiResult_Success);
    check_eq_float(ai_blackboard_get_f64(bb, string_hash_lit("test2")), 42, 1e-6f);
  }

  teardown() { ai_blackboard_destroy(bb); }
}
