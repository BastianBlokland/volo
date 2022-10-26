#pragma once
#include "ai_result.h"

// Forward declare from 'asset_behavior.h'.
typedef struct sAssetAiNode AssetAiNode;

/**
 * Interface for tracing node evaluation.
 */
typedef struct sAiTracer {
  void (*begin)(struct sAiTracer*, const AssetAiNode*);
  void (*end)(struct sAiTracer*, const AssetAiNode*, AiResult);
} AiTracer;
