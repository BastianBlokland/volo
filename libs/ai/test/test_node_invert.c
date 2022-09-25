#include "ai.h"
#include "ai_tracer_count.h"
#include "asset_behavior.h"
#include "check_spec.h"
#include "core_alloc.h"

spec(node_invert) {
  AiBlackboard* bb = null;
  AiTracerCount tracer;

  setup() {
    bb     = ai_blackboard_create(g_alloc_heap);
    tracer = ai_tracer_count();
  }

  it("evaluates to success when child evaluates to failure") {
    const AssetBehavior child    = {.type = AssetBehavior_Failure};
    const AssetBehavior behavior = {
        .type        = AssetBehavior_Invert,
        .data_invert = {.child = &child},
    };
    check(ai_eval(&behavior, bb, &tracer.api) == AiResult_Success);
    check_eq_int(tracer.count, 2);
  }

  it("evaluates to failure when child evaluates to success") {
    const AssetBehavior child    = {.type = AssetBehavior_Success};
    const AssetBehavior behavior = {
        .type        = AssetBehavior_Invert,
        .data_invert = {.child = &child},
    };
    check(ai_eval(&behavior, bb, &tracer.api) == AiResult_Failure);
    check_eq_int(tracer.count, 2);
  }

  it("always evaluates the child node") {
    const AssetBehavior child = {
        .type = AssetBehavior_KnowledgeSet,
        .data_knowledgeset =
            {
                .key   = string_lit("test"),
                .value = {.type = AssetKnowledgeSource_Number, .data_number.value = 42.42},
            },
    };
    const AssetBehavior behavior = {
        .type        = AssetBehavior_Invert,
        .data_invert = {.child = &child},
    };
    check(ai_eval(&behavior, bb, &tracer.api) == AiResult_Failure);
    check_eq_int(tracer.count, 2);
    check_eq_float(ai_blackboard_get_f64(bb, string_hash_lit("test")), 42.42, 1e-6f);
  }

  teardown() { ai_blackboard_destroy(bb); }
}
