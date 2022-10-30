#pragma once
#include "ai_result.h"

// Forward declare from 'asset_behavior.h'.
typedef struct sAssetAiNode AssetAiNode;
typedef u16                 AssetAiNodeId;

// Forward declare from 'ai_eval.h'.
typedef struct sAiEvalContext AiEvalContext;

/**
 * Interface for tracing node evaluation.
 */
typedef struct sAiTracer {
  void (*begin)(const AiEvalContext*, AssetAiNodeId);
  void (*end)(const AiEvalContext*, AssetAiNodeId, AiResult);
} AiTracer;
