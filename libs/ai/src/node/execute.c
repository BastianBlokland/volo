#include "ai_eval.h"
#include "asset_behavior.h"
#include "core_diag.h"
#include "log_logger.h"
#include "script_eval.h"

AiResult ai_node_execute_eval(const AiEvalContext* ctx, const AssetAiNodeId nodeId) {
  const AssetAiNode* def = &ctx->nodeDefs[nodeId];
  diag_assert(def->type == AssetAiNode_Execute);
  diag_assert(ctx->scriptDoc);

  const ScriptExpr expr = def->data_execute.scriptExpr;

  ScriptBinder*          binder  = null;
  void*                  bindCtx = null;
  const ScriptEvalResult evalRes = script_eval(ctx->scriptDoc, ctx->memory, expr, binder, bindCtx);

  if (UNLIKELY(evalRes.type != ScriptResult_Success)) {
    const String err = script_result_str(evalRes.type);
    log_w("Runtime error during AI execution node", log_param("error", fmt_text(err)));
  }

  return AiResult_Success;
}
