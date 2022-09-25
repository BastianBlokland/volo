#pragma once
#include "ai_tracer.h"
#include "asset_behavior.h"
#include "core_string.h"

// Forward declare from 'core_alloc.h'.
typedef struct sAllocator Allocator;

/**
 * Tracer that records node execution information.
 *
 * Recorded information per node:
 * - Type ('AssetBehaviorType' of the node).
 * - Name (Optional name of the node).
 * - Result (Evaluation result of the node).
 * - Depth (Execution hierarchy depth).
 *
 * Nodes are in evaluation order so hierarchy can be inferred from the order + node depth.
 */

typedef struct sAiTracerRecord AiTracerRecord;

AiTracerRecord*   ai_tracer_record_create(Allocator*);
void              ai_tracer_record_destroy(AiTracerRecord*);
void              ai_tracer_record_reset(AiTracerRecord*);
AiTracer*         ai_tracer_record_api(AiTracerRecord*);
u32               ai_tracer_record_count(const AiTracerRecord*);
AssetBehaviorType ai_tracer_record_type(const AiTracerRecord*, u32 nodeIndex);
String            ai_tracer_record_name(const AiTracerRecord*, u32 nodeIndex);
AiResult          ai_tracer_record_result(const AiTracerRecord*, u32 nodeIndex);
u8                ai_tracer_record_depth(const AiTracerRecord*, u32 nodeIndex);
