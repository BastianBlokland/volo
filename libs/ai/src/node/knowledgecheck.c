#include "ai_blackboard.h"
#include "ai_eval.h"
#include "asset_behavior.h"
#include "core_diag.h"

AiResult ai_node_knowledgecheck_eval(const AssetBehavior* behavior, AiBlackboard* bb) {
  diag_assert(behavior->type == AssetBehavior_KnowledgeCheck);

  diag_assert_msg(behavior->data_knowledgecheck.key.size, "Knowledge key cannot be empty");

  // TODO: Keys should be pre-hashed in the behavior asset.
  const StringHash keyHash = string_hash(behavior->data_knowledgecheck.key);

  return ai_blackboard_exists(bb, keyHash) ? AiResult_Success : AiResult_Failure;
}
