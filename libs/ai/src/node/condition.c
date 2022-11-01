#include "ai_eval.h"
#include "asset_behavior.h"
#include "core_diag.h"
#include "script_eval.h"

AiResult ai_node_condition_eval(const AiEvalContext* ctx, const AssetAiNodeId nodeId) {
  const AssetAiNode* def = &ctx->nodeDefs[nodeId];
  diag_assert(def->type == AssetAiNode_Condition);
  diag_assert(ctx->scriptDoc);

  const ScriptExpr expr  = def->data_condition.scriptExpr;
  const ScriptVal  value = script_eval_readonly(ctx->scriptDoc, ctx->memory, expr);

  return script_truthy(value) ? AiResult_Success : AiResult_Failure;
}
