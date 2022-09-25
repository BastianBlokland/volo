#include "ai_tracer_record.h"
#include "asset_behavior.h"
#include "core_alloc.h"
#include "core_diag.h"
#include "core_dynarray.h"

#define ai_tracer_transient_chunk_size (512 * usize_byte)
#define ai_tracer_max_depth 16

typedef enum {
  AiTracerNode_Running = 1 << 0, // NOTE: Only used for internal assertions at the moment.
} AiTracerNodeFlags;

typedef struct {
  AiResult          result : 8; // Uninitialized if the node is still running.
  u8                depth;
  AiTracerNodeFlags flags : 8;
  AssetBehaviorType type;
  String            name;
} AiTracerNode;

typedef struct sAiTracerRecord {
  AiTracer   api;
  DynArray   nodes; // AiTracerNode[]
  Allocator* alloc;
  Allocator* allocTransient;
  u32        depth;
  u32        stack[ai_tracer_max_depth];
} AiTracerRecord;

static AiTracerNode* tracer_observe(AiTracerRecord* tracer, const u32 nodeIndex) {
  AiTracerNode* node = dynarray_at_t(&tracer->nodes, nodeIndex, AiTracerNode);
  diag_assert(!(node->flags & AiTracerNode_Running));
  return node;
}

static void tracer_begin(AiTracer* tracer, const AssetBehavior* behavior) {
  AiTracerRecord* tracerRecord = (AiTracerRecord*)tracer;

  const u32     nodeIndex = (u32)tracerRecord->nodes.size;
  AiTracerNode* node      = dynarray_push_t(&tracerRecord->nodes, AiTracerNode);
  node->depth             = tracerRecord->depth;
  node->flags             = AiTracerNode_Running;
  node->type              = behavior->type;

  // TODO: Add error handling when the transient allocator runs out of space.
  node->name = string_maybe_dup(tracerRecord->allocTransient, behavior->name);

  if (UNLIKELY(tracerRecord->depth == ai_tracer_max_depth)) {
    diag_crash_msg("Ai node depth limit ({}) exceeded", fmt_int(ai_tracer_max_depth));
  }
  tracerRecord->stack[tracerRecord->depth++] = nodeIndex;
}

static void tracer_end(AiTracer* tracer, const AssetBehavior* behavior, const AiResult result) {
  AiTracerRecord* tracerRecord = (AiTracerRecord*)tracer;
  (void)behavior;

  const u32     activeNodeIdx = tracerRecord->stack[--tracerRecord->depth];
  AiTracerNode* activeNode    = dynarray_at_t(&tracerRecord->nodes, activeNodeIdx, AiTracerNode);
  diag_assert(activeNode->type = behavior->type);
  diag_assert(activeNode->flags & AiTracerNode_Running);

  activeNode->result = result;
  activeNode->flags &= ~AiTracerNode_Running;
}

AiTracerRecord* ai_tracer_record_create(Allocator* alloc) {
  AiTracerRecord* tracer = alloc_alloc_t(alloc, AiTracerRecord);

  *tracer = (AiTracerRecord){
      .api =
          {
              .begin = &tracer_begin,
              .end   = &tracer_end,
          },
      .nodes = dynarray_create_t(alloc, AiTracerNode, 32),
      .alloc = alloc,
      .allocTransient =
          alloc_chunked_create(alloc, alloc_bump_create, ai_tracer_transient_chunk_size),
  };
  return tracer;
}

void ai_tracer_record_destroy(AiTracerRecord* tracer) {
  AiTracerRecord* tracerRecord = (AiTracerRecord*)tracer;
  dynarray_destroy(&tracerRecord->nodes);
  alloc_chunked_destroy(tracer->allocTransient);
  alloc_free_t(tracerRecord->alloc, tracerRecord);
}

void ai_tracer_record_reset(AiTracerRecord* tracer) {
  dynarray_clear(&tracer->nodes);
  alloc_reset(tracer->allocTransient);
  tracer->depth = 0;
}

AiTracer* ai_tracer_record_api(AiTracerRecord* tracer) { return (AiTracer*)tracer; }

u32 ai_tracer_record_count(AiTracerRecord* tracer) {
  diag_assert_msg(!tracer->depth, "Unable to observe the tracer: nodes still running");
  return (u32)tracer->nodes.size;
}

AssetBehaviorType ai_tracer_record_type(AiTracerRecord* tracer, const u32 nodeIndex) {
  return tracer_observe(tracer, nodeIndex)->type;
}

String ai_tracer_record_name(AiTracerRecord* tracer, const u32 nodeIndex) {
  return tracer_observe(tracer, nodeIndex)->name;
}

AiResult ai_tracer_record_result(AiTracerRecord* tracer, const u32 nodeIndex) {
  return tracer_observe(tracer, nodeIndex)->result;
}

u8 ai_tracer_record_depth(AiTracerRecord* tracer, const u32 nodeIndex) {
  return tracer_observe(tracer, nodeIndex)->depth;
}
