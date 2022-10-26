#include "ai_eval.h"
#include "asset_behavior.h"
#include "core_diag.h"

AiResult ai_node_try_eval(const AssetAiNode* nodeDef, AiBlackboard* bb, AiTracer* tracer) {
  diag_assert(nodeDef->type == AssetAiNode_Try);

  switch (ai_eval(nodeDef->data_try.child, bb, tracer)) {
  case AiResult_Running:
    return AiResult_Running;
  case AiResult_Success:
    return AiResult_Success;
  case AiResult_Failure:
    return AiResult_Running;
  }
  UNREACHABLE
}
