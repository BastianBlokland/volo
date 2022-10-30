#pragma once
#include "ai_value.h"

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
 * Query and update knowledge.
 * Pre-condition: key != 0.
 */
AiValue ai_blackboard_get(const AiBlackboard*, StringHash key);
void    ai_blackboard_set(AiBlackboard*, StringHash key, AiValue);
void    ai_blackboard_set_null(AiBlackboard*, StringHash key);

/**
 * Iterator for iterating blackboard keys.
 * NOTE: Iterator is invalidated when new entries are inserted.
 */
typedef struct {
  StringHash key; // '0' indicates that no more keys are found.
  u32        next;
} AiBlackboardItr;

AiBlackboardItr ai_blackboard_begin(const AiBlackboard*);
AiBlackboardItr ai_blackboard_next(const AiBlackboard*, AiBlackboardItr);
