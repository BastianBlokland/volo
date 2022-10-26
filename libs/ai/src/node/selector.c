#include "ai_eval.h"
#include "asset_behavior.h"
#include "core_array.h"
#include "core_diag.h"

AiResult ai_node_selector_eval(const AssetAiNode* nodeDef, AiBlackboard* bb, AiTracer* tracer) {
  diag_assert(nodeDef->type == AssetAiNode_Selector);

  array_ptr_for_t(nodeDef->data_selector.children, AssetAiNode, child) {
    switch (ai_eval(child, bb, tracer)) {
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
