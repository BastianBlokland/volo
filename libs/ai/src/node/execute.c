#include "ai_eval.h"
#include "asset_behavior.h"
#include "core_diag.h"
#include "script_eval.h"

AiResult ai_node_execute_eval(const AiEvalContext* ctx, const AssetAiNodeId nodeId) {
  const AssetAiNode* def = &ctx->nodeDefs[nodeId];
  diag_assert(def->type == AssetAiNode_Execute);
  diag_assert(ctx->scriptDoc);

  const ScriptExpr expr = def->data_execute.scriptExpr;
  script_eval(ctx->scriptDoc, ctx->memory, expr);

  return AiResult_Success;
}
