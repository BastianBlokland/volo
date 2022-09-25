#include "ai_eval.h"
#include "asset_behavior.h"
#include "core_diag.h"

AiResult ai_node_invert_eval(const AssetBehavior* behavior, AiBlackboard* bb, AiTracer* tracer) {
  diag_assert(behavior->type == AssetBehavior_Invert);

  switch (ai_eval(behavior->data_invert.child, bb, tracer)) {
  case AiResult_Success:
    return AiResult_Failure;
  case AiResult_Failure:
    return AiResult_Success;
  }
  UNREACHABLE
}
