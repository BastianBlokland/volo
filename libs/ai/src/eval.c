#include "ai_eval.h"
#include "asset_behavior.h"
#include "core_array.h"

#define AI_NODES                                                                                   \
  X(AssetBehavior_Failure, ai_node_failure_eval)                                                   \
  X(AssetBehavior_Invert, ai_node_invert_eval)                                                     \
  X(AssetBehavior_KnowledgeCheck, ai_node_knowledgecheck_eval)                                     \
  X(AssetBehavior_KnowledgeClear, ai_node_knowledgeclear_eval)                                     \
  X(AssetBehavior_KnowledgeSet, ai_node_knowledgeset_eval)                                         \
  X(AssetBehavior_Parallel, ai_node_parallel_eval)                                                 \
  X(AssetBehavior_Selector, ai_node_selector_eval)                                                 \
  X(AssetBehavior_Sequence, ai_node_sequence_eval)                                                 \
  X(AssetBehavior_Success, ai_node_success_eval)

typedef AiResult (*AiNodeEval)(const AssetBehavior*, AiBlackboard*);

#define X(_ASSET_, _FUNC_EVAL_) AiResult _FUNC_EVAL_(const AssetBehavior*, AiBlackboard*);
AI_NODES
#undef X

static const AiNodeEval g_node_eval_funcs[] = {
#define X(_ASSET_, _FUNC_EVAL_) [_ASSET_] = &_FUNC_EVAL_,
    AI_NODES
#undef X
};
ASSERT(array_elems(g_node_eval_funcs) == AssetBehavior_Count, "Missing node eval function");

AiResult ai_eval(const AssetBehavior* behavior, AiBlackboard* bb) {
  return g_node_eval_funcs[behavior->type](behavior, bb);
}
