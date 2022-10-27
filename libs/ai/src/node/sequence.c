#include "ai_eval.h"
#include "asset_behavior.h"
#include "core_array.h"
#include "core_diag.h"

AiResult ai_node_sequence_eval(const AiEvalContext* ctx, const AssetAiNode* nodeDef) {
  diag_assert(nodeDef->type == AssetAiNode_Sequence);

  array_ptr_for_t(nodeDef->data_sequence.children, AssetAiNode, child) {
    switch (ai_eval(ctx, child)) {
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
