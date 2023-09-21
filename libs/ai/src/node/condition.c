#include "ai_eval.h"
#include "asset_behavior.h"
#include "core_diag.h"
#include "script_eval.h"

AiResult ai_node_condition_eval(const AiEvalContext* ctx, const AssetAiNodeId nodeId) {
  const AssetAiNode* def = &ctx->nodeDefs[nodeId];
  diag_assert(def->type == AssetAiNode_Condition);
  diag_assert(ctx->scriptDoc);

  const ScriptExpr       expr = def->data_condition.scriptExpr;
  const ScriptEvalResult res  = script_eval_readonly(ctx->scriptDoc, ctx->memory, expr);

  // TODO: Handle evaluation runtime errors.

  return script_truthy(res.val) ? AiResult_Success : AiResult_Failure;
}
