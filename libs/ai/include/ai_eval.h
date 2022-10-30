#pragma once
#include "ai_result.h"

// Forward declare from 'asset_behavior.h'.
typedef struct sAssetAiNode AssetAiNode;
typedef u16                 AssetAiNodeId;

// Forward declare from 'ai_blackboard.h'.
typedef struct sAiBlackboard AiBlackboard;

// Forward declare from 'ai_tracer.h'.
typedef struct sAiTracer AiTracer;

typedef struct sAiEvalContext {
  AiBlackboard*      memory;
  AiTracer*          tracer; // [Optional].
  const AssetAiNode* nodeDefs;
  const String*      nodeNames; // [Optional].
} AiEvalContext;

AiResult ai_eval(const AiEvalContext*, AssetAiNodeId);
