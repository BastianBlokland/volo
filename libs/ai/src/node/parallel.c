#include "ai_eval.h"
#include "asset_behavior.h"
#include "core_array.h"
#include "core_diag.h"

AiResult ai_node_parallel_eval(const AssetBehavior* behavior, AiBlackboard* bb, AiTracer* tracer) {
  diag_assert(behavior->type == AssetBehavior_Parallel);

  AiResult result = AiResult_Failure;
  array_ptr_for_t(behavior->data_parallel.children, AssetBehavior, child) {
    switch (ai_eval(child, bb, tracer)) {
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
