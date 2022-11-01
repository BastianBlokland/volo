#pragma once
#include "ai_result.h"

// Forward declare from 'asset_behavior.h'.
typedef struct sAssetAiNode AssetAiNode;
typedef u16                 AssetAiNodeId;

// Forward declare from 'script_mem.h'.
typedef struct sScriptMem ScriptMem;

// Forward declare from 'script_doc.h'.
typedef struct sScriptDoc ScriptDoc;

// Forward declare from 'ai_tracer.h'.
typedef struct sAiTracer AiTracer;

typedef struct sAiEvalContext {
  ScriptMem*         memory;
  AiTracer*          tracer; // [Optional].
  const AssetAiNode* nodeDefs;
  const String*      nodeNames; // [Optional].
  const ScriptDoc*   scriptDoc;
} AiEvalContext;

AiResult ai_eval(const AiEvalContext*, AssetAiNodeId);
