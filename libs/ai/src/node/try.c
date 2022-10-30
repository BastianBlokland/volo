#include "ai_eval.h"
#include "asset_behavior.h"
#include "core_diag.h"

AiResult ai_node_try_eval(const AiEvalContext* ctx, const AssetAiNodeId nodeId) {
  const AssetAiNode* def = &ctx->nodeDefs[nodeId];
  diag_assert(def->type == AssetAiNode_Try);

  switch (ai_eval(ctx, def->data_try.child)) {
  case AiResult_Running:
    return AiResult_Running;
  case AiResult_Success:
    return AiResult_Success;
  case AiResult_Failure:
    return AiResult_Running;
  }
  UNREACHABLE
}
