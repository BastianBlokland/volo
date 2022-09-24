#include "ai.h"
#include "asset_behavior.h"
#include "check_spec.h"
#include "core_alloc.h"
#include "core_array.h"

spec(node_knowledgeclear) {
  AiBlackboard* bb = null;
  AiTracerCount tracer;

  setup() {
    bb     = ai_blackboard_create(g_alloc_heap);
    tracer = ai_tracer_count();
  }

  it("does nothing when evaluated with an empty key array") {
    const AssetBehavior behavior = {
        .type                = AssetBehavior_KnowledgeClear,
        .data_knowledgeclear = {.keys = {0}},
    };
    check(ai_eval(&behavior, bb, &tracer.api) == AiResult_Success);
    check_eq_int(tracer.count, 1);
  }

  it("unset's knowledge when evaluated") {
    ai_blackboard_set_f64(bb, string_hash_lit("test"), 42);
    check_eq_float(ai_blackboard_get_f64(bb, string_hash_lit("test")), 42, 1e-6f);

    const String keysToClear[] = {
        string_lit("test"),
        string_lit("test1"),
        string_lit("test2"),
    };
    const AssetBehavior behavior = {
        .type                = AssetBehavior_KnowledgeClear,
        .data_knowledgeclear = {.keys = {.values = keysToClear, array_elems(keysToClear)}},
    };
    check(ai_eval(&behavior, bb, &tracer.api) == AiResult_Success);
    check_eq_int(tracer.count, 1);
    check_eq_float(ai_blackboard_get_f64(bb, string_hash_lit("test")), 0, 1e-6f);
  }

  teardown() { ai_blackboard_destroy(bb); }
}
