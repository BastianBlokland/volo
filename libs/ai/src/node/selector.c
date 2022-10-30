#include "ai_eval.h"
#include "asset_behavior.h"
#include "core_diag.h"

AiResult ai_node_selector_eval(const AiEvalContext* ctx, const AssetAiNodeId nodeId) {
  const AssetAiNode* def = &ctx->nodeDefs[nodeId];
  diag_assert(def->type == AssetAiNode_Selector);

  AssetAiNodeId c = def->data_selector.childrenBegin;
  for (; !sentinel_check(c); c = ctx->nodeDefs[c].nextSibling) {
    switch (ai_eval(ctx, c)) {
    case AiResult_Running:
      return AiResult_Running;
    case AiResult_Success:
      return AiResult_Success;
    case AiResult_Failure:
      continue;
    }
    UNREACHABLE
  }
  return AiResult_Failure;
}
