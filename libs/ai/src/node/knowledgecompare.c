#include "ai_eval.h"
#include "asset_behavior.h"
#include "core_diag.h"

#include "source_internal.h"

AiResult ai_node_knowledgecompare_eval(const AiEvalContext* ctx, const AssetAiNodeId nodeId) {
  const AssetAiNode* def = &ctx->nodeDefs[nodeId];
  diag_assert(def->type == AssetAiNode_KnowledgeCompare);

  const AiValue value     = ai_blackboard_get(ctx->memory, def->data_knowledgecompare.key);
  const AiValue compValue = ai_source_value(&def->data_knowledgecompare.value, ctx->memory);

  bool result;
  switch (def->data_knowledgecompare.comparison) {
  case AssetAiComparison_Equal:
    result = ai_value_equal(value, compValue);
    break;
  case AssetAiComparison_NotEqual:
    result = !ai_value_equal(value, compValue);
    break;
  case AssetAiComparison_Less:
    result = ai_value_less(value, compValue);
    break;
  case AssetAiComparison_LessOrEqual:
    result = !ai_value_greater(value, compValue);
    break;
  case AssetAiComparison_Greater:
    result = ai_value_greater(value, compValue);
    break;
  case AssetAiComparison_GreaterOrEqual:
    result = !ai_value_less(value, compValue);
    break;
  }
  return result ? AiResult_Success : AiResult_Failure;
}
