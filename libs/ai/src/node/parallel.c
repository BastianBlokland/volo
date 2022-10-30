#include "ai_eval.h"
#include "asset_behavior.h"
#include "core_diag.h"

AiResult ai_node_parallel_eval(const AiEvalContext* ctx, const AssetAiNodeId nodeId) {
  const AssetAiNode* def = &ctx->nodeDefs[nodeId];
  diag_assert(def->type == AssetAiNode_Parallel);

  AiResult result = AiResult_Failure;

  AssetAiNodeId c = def->data_parallel.childrenBegin;
  for (; !sentinel_check(c); c = ctx->nodeDefs[c].nextSibling) {
    switch (ai_eval(ctx, c)) {
    case AiResult_Running:
      if (result == AiResult_Failure) {
        result = AiResult_Running;
      }
      continue;
    case AiResult_Success:
      result = AiResult_Success;
      continue;
    case AiResult_Failure:
      continue;
    }
    UNREACHABLE
  }
  return result;
}
