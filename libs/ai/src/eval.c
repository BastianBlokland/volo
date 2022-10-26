#include "ai_eval.h"
#include "ai_tracer.h"
#include "asset_behavior.h"
#include "core_array.h"

#define AI_NODES                                                                                   \
  X(AssetAiNode_Failure, ai_node_failure_eval)                                                     \
  X(AssetAiNode_Invert, ai_node_invert_eval)                                                       \
  X(AssetAiNode_KnowledgeCompare, ai_node_knowledgecompare_eval)                                   \
  X(AssetAiNode_KnowledgeSet, ai_node_knowledgeset_eval)                                           \
  X(AssetAiNode_Parallel, ai_node_parallel_eval)                                                   \
  X(AssetAiNode_Repeat, ai_node_repeat_eval)                                                       \
  X(AssetAiNode_Running, ai_node_running_eval)                                                     \
  X(AssetAiNode_Selector, ai_node_selector_eval)                                                   \
  X(AssetAiNode_Sequence, ai_node_sequence_eval)                                                   \
  X(AssetAiNode_Success, ai_node_success_eval)                                                     \
  X(AssetAiNode_Try, ai_node_try_eval)

typedef AiResult (*AiNodeEval)(const AssetAiNode*, AiBlackboard*, AiTracer*);

#define X(_ASSET_, _FUNC_EVAL_) AiResult _FUNC_EVAL_(const AssetAiNode*, AiBlackboard*, AiTracer*);
AI_NODES
#undef X

static const AiNodeEval g_node_eval_funcs[] = {
#define X(_ASSET_, _FUNC_EVAL_) [_ASSET_] = &_FUNC_EVAL_,
    AI_NODES
#undef X
};
ASSERT(array_elems(g_node_eval_funcs) == AssetAiNode_Count, "Missing node eval function");

AiResult ai_eval(const AssetAiNode* nodeDef, AiBlackboard* bb, AiTracer* tracer) {
  if (tracer) {
    tracer->begin(tracer, nodeDef);
  }
  const AiResult result = g_node_eval_funcs[nodeDef->type](nodeDef, bb, tracer);
  if (tracer) {
    tracer->end(tracer, nodeDef, result);
  }
  return result;
}
