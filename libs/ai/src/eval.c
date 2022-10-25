#include "ai_eval.h"
#include "ai_tracer.h"
#include "asset_behavior.h"
#include "core_array.h"

#define AI_NODES                                                                                   \
  X(AssetBehavior_Failure, ai_node_failure_eval)                                                   \
  X(AssetBehavior_Invert, ai_node_invert_eval)                                                     \
  X(AssetBehavior_KnowledgeCompare, ai_node_knowledgecompare_eval)                                 \
  X(AssetBehavior_KnowledgeSet, ai_node_knowledgeset_eval)                                         \
  X(AssetBehavior_Parallel, ai_node_parallel_eval)                                                 \
  X(AssetBehavior_Repeat, ai_node_repeat_eval)                                                     \
  X(AssetBehavior_Running, ai_node_running_eval)                                                   \
  X(AssetBehavior_Selector, ai_node_selector_eval)                                                 \
  X(AssetBehavior_Sequence, ai_node_sequence_eval)                                                 \
  X(AssetBehavior_Success, ai_node_success_eval)                                                   \
  X(AssetBehavior_Try, ai_node_try_eval)

typedef AiResult (*AiNodeEval)(const AssetBehavior*, AiBlackboard*, AiTracer*);

#define X(_ASSET_, _FUNC_EVAL_)                                                                    \
  AiResult _FUNC_EVAL_(const AssetBehavior*, AiBlackboard*, AiTracer*);
AI_NODES
#undef X

static const AiNodeEval g_node_eval_funcs[] = {
#define X(_ASSET_, _FUNC_EVAL_) [_ASSET_] = &_FUNC_EVAL_,
    AI_NODES
#undef X
};
ASSERT(array_elems(g_node_eval_funcs) == AssetBehavior_Count, "Missing node eval function");

AiResult ai_eval(const AssetBehavior* behavior, AiBlackboard* bb, AiTracer* tracer) {
  if (tracer) {
    tracer->begin(tracer, behavior);
  }
  const AiResult result = g_node_eval_funcs[behavior->type](behavior, bb, tracer);
  if (tracer) {
    tracer->end(tracer, behavior, result);
  }
  return result;
}
