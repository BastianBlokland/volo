#include "ai.h"
#include "ai_tracer_record.h"
#include "check_spec.h"
#include "core_alloc.h"
#include "core_array.h"

spec(tracer_record) {
  AiBlackboard*   bb = null;
  AiTracerRecord* tracer;

  setup() {
    bb     = ai_blackboard_create(g_alloc_heap);
    tracer = ai_tracer_record_create(g_alloc_heap);
  }

  it("has no registered nodes before evaluating") {
    check_eq_int(ai_tracer_record_count(tracer), 0);

    ai_tracer_record_reset(tracer);
    check_eq_int(ai_tracer_record_count(tracer), 0);
  }

  it("can record information for a single node") {
    const AssetBehavior behavior = {.type = AssetBehavior_Success};
    check(ai_eval(&behavior, bb, ai_tracer_record_api(tracer)) == AiResult_Success);

    check_eq_int(ai_tracer_record_count(tracer), 1);
    check_eq_int(ai_tracer_record_type(tracer, 0), AssetBehavior_Success);
    check_eq_string(ai_tracer_record_name(tracer, 0), string_empty);
    check_eq_int(ai_tracer_record_result(tracer, 0), AiResult_Success);
    check_eq_int(ai_tracer_record_depth(tracer, 0), 0);
  }

  it("can record information for a named node") {
    const AssetBehavior behavior = {
        .type = AssetBehavior_Success,
        .name = string_lit("Hello World"),
    };
    check(ai_eval(&behavior, bb, ai_tracer_record_api(tracer)) == AiResult_Success);

    check_eq_int(ai_tracer_record_count(tracer), 1);
    check_eq_string(ai_tracer_record_name(tracer, 0), string_lit("Hello World"));
  }

  it("can record information for a node with child nodes") {
    const AssetBehavior children[] = {
        {.type = AssetBehavior_Failure},
        {.type = AssetBehavior_Success},
        {.type = AssetBehavior_Failure},
    };
    const AssetBehavior behavior = {
        .type          = AssetBehavior_Selector,
        .data_selector = {.children = {.values = children, array_elems(children)}},
    };
    check(ai_eval(&behavior, bb, ai_tracer_record_api(tracer)) == AiResult_Success);
    check_eq_int(ai_tracer_record_count(tracer), 3);

    // Selector node.
    check_eq_int(ai_tracer_record_type(tracer, 0), AssetBehavior_Selector);
    check_eq_string(ai_tracer_record_name(tracer, 0), string_empty);
    check_eq_int(ai_tracer_record_result(tracer, 0), AiResult_Success);
    check_eq_int(ai_tracer_record_depth(tracer, 0), 0);

    // Child 0.
    check_eq_int(ai_tracer_record_type(tracer, 1), AssetBehavior_Failure);
    check_eq_string(ai_tracer_record_name(tracer, 1), string_empty);
    check_eq_int(ai_tracer_record_result(tracer, 1), AiResult_Failure);
    check_eq_int(ai_tracer_record_depth(tracer, 1), 1);

    // Child 1.
    check_eq_int(ai_tracer_record_type(tracer, 2), AssetBehavior_Success);
    check_eq_string(ai_tracer_record_name(tracer, 2), string_empty);
    check_eq_int(ai_tracer_record_result(tracer, 2), AiResult_Success);
    check_eq_int(ai_tracer_record_depth(tracer, 2), 1);
  }

  teardown() {
    ai_blackboard_destroy(bb);
    ai_tracer_record_destroy(tracer);
  }
}
