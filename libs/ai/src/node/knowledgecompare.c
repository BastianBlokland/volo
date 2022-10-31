#include "ai_eval.h"
#include "asset_behavior.h"
#include "core_diag.h"

#include "source_internal.h"

AiResult ai_node_knowledgecompare_eval(const AiEvalContext* ctx, const AssetAiNodeId nodeId) {
  const AssetAiNode* def = &ctx->nodeDefs[nodeId];
  diag_assert(def->type == AssetAiNode_KnowledgeCompare);

  const ScriptVal value     = script_mem_get(ctx->memory, def->data_knowledgecompare.key);
  const ScriptVal compValue = ai_source_value(&def->data_knowledgecompare.value, ctx->memory);

  bool result;
  switch (def->data_knowledgecompare.comparison) {
  case AssetAiComparison_Equal:
    result = script_val_equal(value, compValue);
    break;
  case AssetAiComparison_NotEqual:
    result = !script_val_equal(value, compValue);
    break;
  case AssetAiComparison_Less:
    result = script_val_less(value, compValue);
    break;
  case AssetAiComparison_LessOrEqual:
    result = !script_val_greater(value, compValue);
    break;
  case AssetAiComparison_Greater:
    result = script_val_greater(value, compValue);
    break;
  case AssetAiComparison_GreaterOrEqual:
    result = !script_val_less(value, compValue);
    break;
  }
  return result ? AiResult_Success : AiResult_Failure;
}
