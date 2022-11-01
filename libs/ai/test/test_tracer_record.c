#include "ai_eval.h"
#include "ai_tracer_record.h"
#include "check_spec.h"
#include "core_alloc.h"
#include "core_array.h"
#include "script_mem.h"

spec(tracer_record) {
  ScriptMem*      memory = null;
  AiTracerRecord* tracer;

  setup() {
    memory = script_mem_create(g_alloc_heap);
    tracer = ai_tracer_record_create(g_alloc_heap);
  }

  it("has no registered nodes before evaluating") {
    check_eq_int(ai_tracer_record_count(tracer), 0);

    ai_tracer_record_reset(tracer);
    check_eq_int(ai_tracer_record_count(tracer), 0);
  }

  it("can record information for a single node") {
    const AssetAiNode nodeDefs[] = {
        {.type = AssetAiNode_Success, .nextSibling = sentinel_u16},
    };
    const AiEvalContext ctx = {
        .memory   = memory,
        .tracer   = ai_tracer_record_api(tracer),
        .nodeDefs = nodeDefs,
    };
    check(ai_eval(&ctx, AssetAiNodeRoot) == AiResult_Success);

    check_eq_int(ai_tracer_record_count(tracer), 1);
    check_eq_int(ai_tracer_record_type(tracer, 0), AssetAiNode_Success);
    check_eq_string(ai_tracer_record_name(tracer, 0), string_empty);
    check_eq_int(ai_tracer_record_result(tracer, 0), AiResult_Success);
    check_eq_int(ai_tracer_record_depth(tracer, 0), 0);
  }

  it("can record information for a named node") {
    const AssetAiNode nodeDefs[] = {
        {.type = AssetAiNode_Success, .nextSibling = sentinel_u16},
    };
    const String nodeNames[] = {string_lit("Hello World")};
    ASSERT(array_elems(nodeDefs) == array_elems(nodeNames), "Node count mismatch");

    const AiEvalContext ctx = {
        .memory    = memory,
        .tracer    = ai_tracer_record_api(tracer),
        .nodeDefs  = nodeDefs,
        .nodeNames = nodeNames,
    };
    check(ai_eval(&ctx, AssetAiNodeRoot) == AiResult_Success);

    check_eq_int(ai_tracer_record_count(tracer), 1);
    check_eq_string(ai_tracer_record_name(tracer, 0), string_lit("Hello World"));
  }

  it("can record information for a node with child nodes") {
    const AssetAiNode nodeDefs[] = {
        {
            .type          = AssetAiNode_Selector,
            .nextSibling   = sentinel_u16,
            .data_selector = {.childrenBegin = 1},
        },
        {.type = AssetAiNode_Failure, .nextSibling = 2},
        {.type = AssetAiNode_Success, .nextSibling = 3},
        {.type = AssetAiNode_Failure, .nextSibling = sentinel_u16},
    };
    const AiEvalContext ctx = {
        .memory   = memory,
        .tracer   = ai_tracer_record_api(tracer),
        .nodeDefs = nodeDefs,
    };
    check(ai_eval(&ctx, AssetAiNodeRoot) == AiResult_Success);
    check_eq_int(ai_tracer_record_count(tracer), 3);

    // Selector node.
    check_eq_int(ai_tracer_record_type(tracer, 0), AssetAiNode_Selector);
    check_eq_string(ai_tracer_record_name(tracer, 0), string_empty);
    check_eq_int(ai_tracer_record_result(tracer, 0), AiResult_Success);
    check_eq_int(ai_tracer_record_depth(tracer, 0), 0);

    // Child 0.
    check_eq_int(ai_tracer_record_type(tracer, 1), AssetAiNode_Failure);
    check_eq_string(ai_tracer_record_name(tracer, 1), string_empty);
    check_eq_int(ai_tracer_record_result(tracer, 1), AiResult_Failure);
    check_eq_int(ai_tracer_record_depth(tracer, 1), 1);

    // Child 1.
    check_eq_int(ai_tracer_record_type(tracer, 2), AssetAiNode_Success);
    check_eq_string(ai_tracer_record_name(tracer, 2), string_empty);
    check_eq_int(ai_tracer_record_result(tracer, 2), AiResult_Success);
    check_eq_int(ai_tracer_record_depth(tracer, 2), 1);
  }

  teardown() {
    script_mem_destroy(memory);
    ai_tracer_record_destroy(tracer);
  }
}
