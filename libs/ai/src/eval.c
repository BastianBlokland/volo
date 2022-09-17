#include "ai_eval.h"
#include "asset_behavior.h"
#include "core_array.h"

typedef AiResult (*AiNodeEval)(const AssetBehavior*, AiBlackboard*);

AiResult ai_node_failure_eval(const AssetBehavior*, AiBlackboard*);
AiResult ai_node_invert_eval(const AssetBehavior*, AiBlackboard*);
AiResult ai_node_knowledgecheck_eval(const AssetBehavior*, AiBlackboard*);
AiResult ai_node_knowledgeclear_eval(const AssetBehavior*, AiBlackboard*);
AiResult ai_node_knowledgeset_eval(const AssetBehavior*, AiBlackboard*);
AiResult ai_node_parallel_eval(const AssetBehavior*, AiBlackboard*);
AiResult ai_node_selector_eval(const AssetBehavior*, AiBlackboard*);
AiResult ai_node_sequence_eval(const AssetBehavior*, AiBlackboard*);
AiResult ai_node_success_eval(const AssetBehavior*, AiBlackboard*);

static const AiNodeEval g_node_eval_funcs[] = {
    [AssetBehavior_Failure]        = &ai_node_failure_eval,
    [AssetBehavior_Invert]         = &ai_node_invert_eval,
    [AssetBehavior_KnowledgeCheck] = &ai_node_knowledgecheck_eval,
    [AssetBehavior_KnowledgeClear] = &ai_node_knowledgeclear_eval,
    [AssetBehavior_KnowledgeSet]   = &ai_node_knowledgeset_eval,
    [AssetBehavior_Parallel]       = &ai_node_parallel_eval,
    [AssetBehavior_Selector]       = &ai_node_selector_eval,
    [AssetBehavior_Sequence]       = &ai_node_sequence_eval,
    [AssetBehavior_Success]        = &ai_node_success_eval,
};
ASSERT(array_elems(g_node_eval_funcs) == AssetBehavior_Count, "Missing node eval function");

AiResult ai_eval(const AssetBehavior* behavior, AiBlackboard* bb) {
  return g_node_eval_funcs[behavior->type](behavior, bb);
}
