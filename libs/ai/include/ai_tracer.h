#pragma once
#include "ai_result.h"
#include "core_types.h"

// Forward declare from 'asset_behavior.h'.
typedef struct sAssetBehavior AssetBehavior;

/**
 * Interface for tracing node evaluation.
 */
typedef struct sAiTracer {
  void (*begin)(struct sAiTracer*, const AssetBehavior*);
  void (*end)(struct sAiTracer*, const AssetBehavior*, AiResult);
  void (*destruct)(struct sAiTracer*); // Optional.
} AiTracer;

/**
 * Destroy a tracer instance.
 */
void ai_tracer_destroy(AiTracer*);

/**
 * Simple tracer that counts the amount of node evaluations.
 * NOTE: Returned by value, doesn't need to be destroyed.
 */

typedef struct {
  AiTracer api;
  u32      count;
} AiTracerCount;

AiTracerCount ai_tracer_count_create();
