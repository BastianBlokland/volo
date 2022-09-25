#pragma once
#include "ai_result.h"

// Forward declare from 'asset_behavior.h'.
typedef struct sAssetBehavior AssetBehavior;

/**
 * Interface for tracing node evaluation.
 */
typedef struct sAiTracer {
  void (*begin)(struct sAiTracer*, const AssetBehavior*);
  void (*end)(struct sAiTracer*, const AssetBehavior*, AiResult);
} AiTracer;
