#include "ai_blackboard.h"
#include "ai_eval.h"
#include "asset_behavior.h"
#include "core_array.h"
#include "core_diag.h"

AiResult
ai_node_knowledgecheck_eval(const AssetBehavior* behavior, AiBlackboard* bb, AiTracer* tracer) {
  diag_assert(behavior->type == AssetBehavior_KnowledgeCheck);
  (void)tracer;

  bool allExist = true;
  array_ptr_for_t(behavior->data_knowledgecheck.keys, String, key) {
    diag_assert_msg(!string_is_empty(*key), "Knowledge key cannot be empty");

    // TODO: Keys should be pre-hashed in the behavior asset.
    const StringHash keyHash = string_hash(*key);

    allExist &= ai_blackboard_get(bb, keyHash).type != AiValueType_None;
  }
  return allExist ? AiResult_Success : AiResult_Failure;
}
