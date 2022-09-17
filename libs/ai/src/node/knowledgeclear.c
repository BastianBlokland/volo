#include "ai_blackboard.h"
#include "ai_eval.h"
#include "asset_behavior.h"
#include "core_diag.h"

AiResult ai_node_knowledgeclear_eval(const AssetBehavior* behavior, AiBlackboard* bb) {
  diag_assert(behavior->type == AssetBehavior_KnowledgeClear);

  diag_assert_msg(behavior->data_knowledgeclear.key.size, "Knowledge key cannot be empty");

  // TODO: Keys should be pre-hashed in the behavior asset.
  const StringHash keyHash = string_hash(behavior->data_knowledgeclear.key);

  ai_blackboard_unset(bb, keyHash);

  return AiResult_Success;
}
