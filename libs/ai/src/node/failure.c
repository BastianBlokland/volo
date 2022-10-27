#include "ai_eval.h"
#include "asset_behavior.h"
#include "core_diag.h"

AiResult ai_node_failure_eval(const AiEvalContext* ctx, const AssetAiNode* nodeDef) {
  diag_assert(nodeDef->type == AssetAiNode_Failure);
  (void)ctx;
  (void)nodeDef;

  return AiResult_Failure;
}
