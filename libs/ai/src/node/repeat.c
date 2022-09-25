#include "ai_eval.h"
#include "asset_behavior.h"
#include "core_diag.h"

AiResult ai_node_repeat_eval(const AssetBehavior* behavior, AiBlackboard* bb, AiTracer* tracer) {
  diag_assert(behavior->type == AssetBehavior_Repeat);

  switch (ai_eval(behavior->data_repeat.child, bb, tracer)) {
  case AiResult_Running:
  case AiResult_Success:
    return AiResult_Running;
  case AiResult_Failure:
    return AiResult_Failure;
  }
  UNREACHABLE
}
