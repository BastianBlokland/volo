#include "ai_blackboard.h"
#include "ai_eval.h"
#include "asset_behavior.h"
#include "core_diag.h"
#include "core_string.h"

AiResult ai_node_knowledgeset_eval(const AssetBehavior* behavior, AiBlackboard* bb) {
  diag_assert(behavior->type == AssetBehaviorType_KnowledgeSet);

  diag_assert_msg(behavior->data_knowledgeset.key.size, "Knowledge key cannot be empty");

  // TODO: Keys should be pre-hashed in the behavior asset.
  const StringHash keyHash = string_hash(behavior->data_knowledgeset.key);

  switch (behavior->data_knowledgeset.value.type) {
  case AssetKnowledgeType_f64:
    ai_blackboard_set_f64(bb, keyHash, behavior->data_knowledgeset.value.data_f64);
    return AiResult_Success;
  case AssetKnowledgeType_Vector:
    ai_blackboard_set_vector(bb, keyHash, behavior->data_knowledgeset.value.data_vector);
    return AiResult_Success;
  }
  UNREACHABLE
}
