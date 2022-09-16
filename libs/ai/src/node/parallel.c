#include "ai_eval.h"
#include "asset_behavior.h"
#include "core_array.h"
#include "core_diag.h"

AiResult ai_node_parallel_eval(const AssetBehavior* behavior, AiBlackboard* bb) {
  diag_assert(behavior->type == AssetBehaviorType_Parallel);

  bool success = false;
  array_ptr_for_t(behavior->data_parallel.children, AssetBehavior, child) {
    switch (ai_eval(child, bb)) {
    case AiResult_Success:
      success = true;
    case AiResult_Failure:
      continue;
    }
    UNREACHABLE
  }
  return success ? AiResult_Success : AiResult_Failure;
}
