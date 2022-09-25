#include "ai_eval.h"
#include "asset_behavior.h"
#include "core_array.h"
#include "core_diag.h"

AiResult ai_node_selector_eval(const AssetBehavior* behavior, AiBlackboard* bb, AiTracer* tracer) {
  diag_assert(behavior->type == AssetBehavior_Selector);

  array_ptr_for_t(behavior->data_selector.children, AssetBehavior, child) {
    switch (ai_eval(child, bb, tracer)) {
    case AiResult_Success:
      return AiResult_Success;
    case AiResult_Failure:
      continue;
    }
    UNREACHABLE
  }
  return AiResult_Failure;
}
