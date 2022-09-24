#pragma once
#include "ai_tracer.h"
#include "core_types.h"

/**
 * Simple tracer that counts the amount of node evaluations.
 * NOTE: Returned by value, doesn't need to be destroyed.
 */

typedef struct {
  AiTracer api;
  u32      count;
} AiTracerCount;

AiTracerCount ai_tracer_count();
