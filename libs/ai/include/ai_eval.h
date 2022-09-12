#pragma once

// Forward declare from 'core_alloc.h'.
typedef struct sAllocator Allocator;

// Forward declare from 'ai_blackboard.h'.
typedef struct sAiBlackboard AiBlackboard;

// Forward declare from 'asset_behavior.h'.
typedef struct sAssetBehavior AssetBehavior;

typedef enum {
  AiResult_Success,
  AiResult_Failure,
} AiResult;

/**
 * Evaluate the behavior against the given blackboard.
 * Existing knowledge is read from the blackboard and new knowledge is stored on the blackboard.
 */
AiResult ai_eval(const AssetBehavior*, AiBlackboard*);
