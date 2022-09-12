#pragma once
#include "core_string.h"
#include "geo_vector.h"

// Forward declare from 'core_alloc.h'.
typedef struct sAllocator Allocator;

/**
 * Blackboard instance for storing knowledge.
 */
typedef struct sAiBlackboard AiBlackboard;

/**
 * Create a new AiBlackboard instance.
 * Destroy using 'ai_blackboard_destroy()'.
 */
AiBlackboard* ai_blackboard_create(Allocator*);

/**
 * Destroy a AiBlackboard instance.
 */
void ai_blackboard_destroy(AiBlackboard*);

/**
 * Update knowledge.
 */
void ai_blackboard_set_f64(AiBlackboard*, StringHash key, f64 value);
void ai_blackboard_set_vector(AiBlackboard*, StringHash key, GeoVector value);

/**
 * Query knowledge.
 */
f64       ai_blackboard_get_f64(const AiBlackboard*, StringHash key, f64 fallback);
GeoVector ai_blackboard_get_vector(const AiBlackboard*, StringHash key, GeoVector fallback);
