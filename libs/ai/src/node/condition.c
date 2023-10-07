#include "ai_eval.h"
#include "asset_behavior.h"
#include "core_diag.h"
#include "log_logger.h"
#include "script_eval.h"

AiResult ai_node_condition_eval(const AiEvalContext* ctx, const AssetAiNodeId nodeId) {
  const AssetAiNode* def = &ctx->nodeDefs[nodeId];
  diag_assert(def->type == AssetAiNode_Condition);
  diag_assert(ctx->scriptDoc);

  const ScriptExpr       expr    = def->data_condition.scriptExpr;
  const ScriptEvalResult evalRes = script_eval_readonly(ctx->scriptDoc, ctx->memory, expr);

  if (UNLIKELY(evalRes.error != ScriptError_None)) {
    const String err = script_error_str(evalRes.error);
    log_w("Runtime error during AI condition node", log_param("error", fmt_text(err)));
  }

  return script_truthy(evalRes.val) ? AiResult_Success : AiResult_Failure;
}
