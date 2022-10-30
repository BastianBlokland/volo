#include "ai_eval.h"
#include "asset_behavior.h"
#include "core_diag.h"

AiResult ai_node_sequence_eval(const AiEvalContext* ctx, const AssetAiNodeId nodeId) {
  const AssetAiNode* def = &ctx->nodeDefs[nodeId];
  diag_assert(def->type == AssetAiNode_Sequence);

  AssetAiNodeId c = def->data_sequence.childrenBegin;
  for (; !sentinel_check(c); c = ctx->nodeDefs[c].nextSibling) {
    switch (ai_eval(ctx, c)) {
    case AiResult_Running:
      return AiResult_Running;
    case AiResult_Success:
      continue;
    case AiResult_Failure:
      return AiResult_Failure;
    }
    UNREACHABLE
  }
  return AiResult_Success;
}
