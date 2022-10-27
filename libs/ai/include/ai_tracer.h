#pragma once
#include "ai_result.h"

// Forward declare from 'asset_behavior.h'.
typedef struct sAssetAiNode AssetAiNode;

// Forward declare from 'ai_eval.h'.
typedef struct sAiEvalContext AiEvalContext;

/**
 * Interface for tracing node evaluation.
 */
typedef struct sAiTracer {
  void (*begin)(const AiEvalContext*, const AssetAiNode*);
  void (*end)(const AiEvalContext*, const AssetAiNode*, AiResult);
} AiTracer;
