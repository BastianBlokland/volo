#pragma once
#include "ai_knowledge.h"

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
