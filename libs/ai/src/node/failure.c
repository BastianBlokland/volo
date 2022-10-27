#include "ai_eval.h"
#include "asset_behavior.h"
#include "core_diag.h"

AiResult ai_node_failure_eval(const AssetAiNode* nodeDef, AiBlackboard* bb, AiTracer* tracer) {
  diag_assert(nodeDef->type == AssetAiNode_Failure);
  (void)nodeDef;
  (void)bb;
  (void)tracer;

  return AiResult_Failure;
}
