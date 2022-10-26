#pragma once
#include "ai_result.h"

// Forward declare from 'core_alloc.h'.
typedef struct sAllocator Allocator;

// Forward declare from 'ai_blackboard.h'.
typedef struct sAiBlackboard AiBlackboard;

// Forward declare from 'ai_tracer.h'.
typedef struct sAiTracer AiTracer;

// Forward declare from 'asset_behavior.h'.
typedef struct sAssetAiNode AssetAiNode;

/**
 * Evaluate the node against the given blackboard.
 * Existing knowledge is read from the blackboard and new knowledge is stored on the blackboard.
 * NOTE: Tracer is optional, pass null if no tracing is desired.
 */
AiResult ai_eval(const AssetAiNode*, AiBlackboard*, AiTracer* tracer);
