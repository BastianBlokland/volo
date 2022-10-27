#include "ai.h"
#include "ai_tracer_count.h"
#include "asset_behavior.h"
#include "check_spec.h"
#include "core_alloc.h"
#include "core_time.h"

#include "utils_internal.h"

spec(node_knowledgeset) {
  AiBlackboard* bb = null;
  AiTracerCount tracer;

  setup() {
    bb     = ai_blackboard_create(g_alloc_heap);
    tracer = ai_tracer_count();
  }

  it("can set f64 knowledge when evaluated") {
    check_eq_value(ai_blackboard_get(bb, string_hash_lit("test")), ai_value_none());

    const AssetAiNode nodeDef = {
        .type = AssetAiNode_KnowledgeSet,
        .data_knowledgeset =
            {
                .key   = string_lit("test"),
                .value = {.type = AssetAiSource_Number, .data_number.value = 42.42},
            },
    };
    const AiEvalContext ctx = {
        .memory = bb,
        .tracer = &tracer.api,
    };
    check(ai_eval(&ctx, &nodeDef) == AiResult_Success);
    check_eq_int(tracer.count, 1);
    check_eq_value(ai_blackboard_get(bb, string_hash_lit("test")), ai_value_f64(42.42));
  }

  it("can set boolean knowledge when evaluated") {
    check_eq_value(ai_blackboard_get(bb, string_hash_lit("test")), ai_value_none());

    const AssetAiNode nodeDef = {
        .type = AssetAiNode_KnowledgeSet,
        .data_knowledgeset =
            {
                .key   = string_lit("test"),
                .value = {.type = AssetAiSource_Bool, .data_bool.value = true},
            },
    };
    const AiEvalContext ctx = {
        .memory = bb,
        .tracer = &tracer.api,
    };
    check(ai_eval(&ctx, &nodeDef) == AiResult_Success);
    check_eq_int(tracer.count, 1);
    check_eq_value(ai_blackboard_get(bb, string_hash_lit("test")), ai_value_bool(true));
  }

  it("can set vector knowledge when evaluated") {
    check_eq_value(ai_blackboard_get(bb, string_hash_lit("test")), ai_value_none());

    const AssetAiNode nodeDef = {
        .type = AssetAiNode_KnowledgeSet,
        .data_knowledgeset =
            {
                .key   = string_lit("test"),
                .value = {.type = AssetAiSource_Vector, .data_vector = {.x = 1, .y = 2, .z = 3}},
            },
    };
    const AiEvalContext ctx = {
        .memory = bb,
        .tracer = &tracer.api,
    };
    check(ai_eval(&ctx, &nodeDef) == AiResult_Success);
    check_eq_int(tracer.count, 1);
    check_eq_value(
        ai_blackboard_get(bb, string_hash_lit("test")), ai_value_vector3(geo_vector(1, 2, 3)));
  }

  it("can set time knowledge when evaluated") {
    check_eq_value(ai_blackboard_get(bb, string_hash_lit("test")), ai_value_none());

    const AssetAiNode nodeDef = {
        .type = AssetAiNode_KnowledgeSet,
        .data_knowledgeset =
            {
                .key   = string_lit("test"),
                .value = {.type = AssetAiSource_Time, .data_time.secondsFromNow = 1.75f},
            },
    };
    const AiEvalContext ctx = {
        .memory = bb,
        .tracer = &tracer.api,
    };
    check(ai_eval(&ctx, &nodeDef) == AiResult_Success);
    check_eq_int(tracer.count, 1);
    check_eq_value(
        ai_blackboard_get(bb, string_hash_lit("test")),
        ai_value_time(time_second + time_milliseconds(750)));
  }

  it("can set knowledge based on other knowledge when evaluated") {
    ai_blackboard_set(bb, string_hash_lit("test1"), ai_value_f64(42));

    const AssetAiNode nodeDef = {
        .type = AssetAiNode_KnowledgeSet,
        .data_knowledgeset =
            {
                .key = string_lit("test2"),
                .value =
                    {
                        .type           = AssetAiSource_Knowledge,
                        .data_knowledge = string_lit("test1"),
                    },
            },
    };
    const AiEvalContext ctx = {
        .memory = bb,
        .tracer = &tracer.api,
    };
    check(ai_eval(&ctx, &nodeDef) == AiResult_Success);
    check_eq_int(tracer.count, 1);
    check_eq_value(ai_blackboard_get(bb, string_hash_lit("test2")), ai_value_f64(42));
  }

  teardown() { ai_blackboard_destroy(bb); }
}
