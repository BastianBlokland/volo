#pragma once
#include "core_string.h"
#include "geo_vector.h"

// Forward declare from 'core_alloc.h'.
typedef struct sAllocator Allocator;

typedef enum {
  AiBlackboardType_Invalid,
  AiBlackboardType_f64,
  AiBlackboardType_Vector,

  AiBlackboardType_Count,
} AiBlackboardType;

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
 * Query the type of a knowledge key.
 * NOTE: Returns 'AiBlackboardType_Invalid' when the knowledge is unset.
 * Pre-condition: key != 0.
 */
AiBlackboardType ai_blackboard_type(AiBlackboard*, StringHash key);

/**
 * Update knowledge.
 * Pre-condition: key != 0.
 */
void ai_blackboard_set_f64(AiBlackboard*, StringHash key, f64 value);
void ai_blackboard_set_vector(AiBlackboard*, StringHash key, GeoVector value);

/**
 * Query knowledge.
 * Pre-condition: key != 0.
 */
f64       ai_blackboard_get_f64(const AiBlackboard*, StringHash key);
GeoVector ai_blackboard_get_vector(const AiBlackboard*, StringHash key);
