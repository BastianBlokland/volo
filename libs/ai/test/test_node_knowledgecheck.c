#include "ai.h"
#include "asset_behavior.h"
#include "check_spec.h"
#include "core_alloc.h"
#include "core_array.h"

spec(node_knowledgecheck) {
  AiBlackboard* bb = null;
  AiTracerCount tracer;

  setup() {
    bb     = ai_blackboard_create(g_alloc_heap);
    tracer = ai_tracer_count();
  }

  it("evaluates to success when given an empty key array") {
    const AssetBehavior behavior = {
        .type                = AssetBehavior_KnowledgeCheck,
        .data_knowledgecheck = {.keys = {0}},
    };
    check(ai_eval(&behavior, bb, &tracer.api) == AiResult_Success);
    check_eq_int(tracer.count, 1);
  }

  it("evaluates to failure when knowledge for any key does not exist") {
    ai_blackboard_set_f64(bb, string_hash_lit("test"), 42);

    const String keysToCheck[] = {
        string_lit("test"),
        string_lit("test1"),
    };
    const AssetBehavior behavior = {
        .type                = AssetBehavior_KnowledgeCheck,
        .data_knowledgecheck = {.keys = {.values = keysToCheck, array_elems(keysToCheck)}},
    };
    check(ai_eval(&behavior, bb, &tracer.api) == AiResult_Failure);
    check_eq_int(tracer.count, 1);
  }

  it("evaluates to success when knowledge for all the keys exists") {
    ai_blackboard_set_f64(bb, string_hash_lit("test"), 42);
    ai_blackboard_set_f64(bb, string_hash_lit("test1"), 1337);

    const String keysToCheck[] = {
        string_lit("test"),
        string_lit("test1"),
    };
    const AssetBehavior behavior = {
        .type                = AssetBehavior_KnowledgeCheck,
        .data_knowledgecheck = {.keys = {.values = keysToCheck, array_elems(keysToCheck)}},
    };
    check(ai_eval(&behavior, bb, &tracer.api) == AiResult_Success);
    check_eq_int(tracer.count, 1);
  }

  teardown() { ai_blackboard_destroy(bb); }
}
