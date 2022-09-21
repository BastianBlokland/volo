#include "ai_blackboard.h"
#include "ai_eval.h"
#include "asset_behavior.h"
#include "core_diag.h"
#include "core_stringtable.h"

AiResult ai_node_knowledgeset_eval(const AssetBehavior* behavior, AiBlackboard* bb) {
  diag_assert(behavior->type == AssetBehavior_KnowledgeSet);

  diag_assert_msg(behavior->data_knowledgeset.key.size, "Knowledge key cannot be empty");

  // TODO: Keys should be pre-hashed in the behavior asset.
  const StringHash keyHash = stringtable_add(g_stringtable, behavior->data_knowledgeset.key);
  const AssetKnowledgeSource* valueSource = &behavior->data_knowledgeset.value;

  switch (valueSource->type) {
  case AssetKnowledgeSource_Number: {
    ai_blackboard_set_f64(bb, keyHash, valueSource->data_number.value);
    return AiResult_Success;
  }
  case AssetKnowledgeSource_Bool: {
    ai_blackboard_set_bool(bb, keyHash, valueSource->data_bool.value);
    return AiResult_Success;
  }
  case AssetKnowledgeSource_Vector: {
    const GeoVector vector = {
        .x = valueSource->data_vector.x,
        .y = valueSource->data_vector.y,
        .z = valueSource->data_vector.z,
        .w = valueSource->data_vector.w,
    };
    ai_blackboard_set_vector(bb, keyHash, vector);
    return AiResult_Success;
  }
  case AssetKnowledgeSource_Knowledge: {
    // TODO: Keys should be pre-hashed in the behavior asset.
    const StringHash srcKeyHash = string_hash(valueSource->data_knowledge.key);
    ai_blackboard_copy(bb, srcKeyHash, keyHash);
    return AiResult_Success;
  }
  }
  UNREACHABLE
}
