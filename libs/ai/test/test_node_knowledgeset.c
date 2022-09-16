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
        .type = AssetBehaviorType_KnowledgeSet,
        .data_knowledgeset =
            {
                .key = string_lit("test"),
                .value =
                    {
                        .type     = AssetKnowledgeType_f64,
                        .data_f64 = 42.42,
                    },
            },
    };
    check(ai_eval(&behavior, bb) == AiResult_Success);
    check_eq_float(ai_blackboard_get_f64(bb, string_hash_lit("test")), 42.42, 1e-6f);
  }

  it("can set vector knowledge when evaluated") {
    check_eq_float(ai_blackboard_get_vector(bb, string_hash_lit("test")).x, 0, 1e-6f);
    check_eq_float(ai_blackboard_get_vector(bb, string_hash_lit("test")).y, 0, 1e-6f);
    check_eq_float(ai_blackboard_get_vector(bb, string_hash_lit("test")).z, 0, 1e-6f);
    check_eq_float(ai_blackboard_get_vector(bb, string_hash_lit("test")).w, 0, 1e-6f);

    const AssetBehavior behavior = {
        .type = AssetBehaviorType_KnowledgeSet,
        .data_knowledgeset =
            {
                .key = string_lit("test"),
                .value =
                    {
                        .type        = AssetKnowledgeType_Vector,
                        .data_vector = geo_vector(1, 2, 3, 4),
                    },
            },
    };
    check(ai_eval(&behavior, bb) == AiResult_Success);
    check_eq_float(ai_blackboard_get_vector(bb, string_hash_lit("test")).x, 1, 1e-6f);
    check_eq_float(ai_blackboard_get_vector(bb, string_hash_lit("test")).y, 2, 1e-6f);
    check_eq_float(ai_blackboard_get_vector(bb, string_hash_lit("test")).z, 3, 1e-6f);
    check_eq_float(ai_blackboard_get_vector(bb, string_hash_lit("test")).w, 4, 1e-6f);
  }

  teardown() { ai_blackboard_destroy(bb); }
}
