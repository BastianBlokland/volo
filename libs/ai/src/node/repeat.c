#include "ai_eval.h"
#include "asset_behavior.h"
#include "core_diag.h"

AiResult ai_node_repeat_eval(const AssetAiNode* nodeDef, AiBlackboard* bb, AiTracer* tracer) {
  diag_assert(nodeDef->type == AssetAiNode_Repeat);

  switch (ai_eval(nodeDef->data_repeat.child, bb, tracer)) {
  case AiResult_Running:
  case AiResult_Success:
    return AiResult_Running;
  case AiResult_Failure:
    return AiResult_Failure;
  }
  UNREACHABLE
}
