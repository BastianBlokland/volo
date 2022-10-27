#include "ai_eval.h"
#include "asset_behavior.h"
#include "core_diag.h"

AiResult ai_node_invert_eval(const AiEvalContext* ctx, const AssetAiNode* nodeDef) {
  diag_assert(nodeDef->type == AssetAiNode_Invert);

  switch (ai_eval(ctx, nodeDef->data_invert.child)) {
  case AiResult_Running:
    return AiResult_Running;
  case AiResult_Success:
    return AiResult_Failure;
  case AiResult_Failure:
    return AiResult_Success;
  }
  UNREACHABLE
}
