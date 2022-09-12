#include "ai_eval.h"
#include "asset_behavior.h"
#include "core_array.h"

typedef AiResult (*AiNodeEval)(const AssetBehavior*, AiBlackboard*);

AiResult ai_node_success_eval(const AssetBehavior*, AiBlackboard*);
AiResult ai_node_failure_eval(const AssetBehavior*, AiBlackboard*);

static const AiNodeEval g_node_eval_funcs[] = {
    [AssetBehaviorType_Success] = &ai_node_success_eval,
    [AssetBehaviorType_Failure] = &ai_node_failure_eval,
};
// ASSERT(array_elems(g_node_eval_funcs) == AssetBehaviorType_Count, "Missing node eval function");

AiResult ai_eval(const AssetBehavior* behavior, AiBlackboard* bb) {
  return g_node_eval_funcs[behavior->type](behavior, bb);
}
