#include "ai_blackboard.h"
#include "ai_eval.h"
#include "asset_behavior.h"
#include "core_array.h"
#include "core_diag.h"

AiResult
ai_node_knowledgeclear_eval(const AssetBehavior* behavior, AiBlackboard* bb, AiTracer* tracer) {
  diag_assert(behavior->type == AssetBehavior_KnowledgeClear);
  (void)tracer;

  array_ptr_for_t(behavior->data_knowledgeclear.keys, String, key) {
    diag_assert_msg(!string_is_empty(*key), "Knowledge key cannot be empty");

    // TODO: Keys should be pre-hashed in the behavior asset.
    const StringHash keyHash = string_hash(*key);

    ai_blackboard_set_none(bb, keyHash);
  }

  return AiResult_Success;
}
