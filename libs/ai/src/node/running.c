#include "ai_eval.h"
#include "asset_behavior.h"
#include "core_diag.h"

AiResult ai_node_running_eval(const AssetBehavior* behavior, AiBlackboard* bb, AiTracer* tracer) {
  diag_assert(behavior->type == AssetBehavior_Running);
  (void)behavior;
  (void)bb;
  (void)tracer;

  return AiResult_Running;
}
