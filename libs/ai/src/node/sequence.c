#include "ai_eval.h"
#include "asset_behavior.h"
#include "core_array.h"
#include "core_diag.h"

AiResult ai_node_sequence_eval(const AssetBehavior* behavior, AiBlackboard* bb) {
  diag_assert(behavior->type == AssetBehaviorType_Sequence);

  array_ptr_for_t(behavior->data_sequence.children, AssetBehavior, child) {
    switch (ai_eval(child, bb)) {
    case AiResult_Success:
      continue;
    case AiResult_Failure:
      return AiResult_Failure;
    }
    UNREACHABLE
  }
  return AiResult_Success;
}
