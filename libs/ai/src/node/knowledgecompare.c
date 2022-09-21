#include "ai_blackboard.h"
#include "ai_eval.h"
#include "asset_behavior.h"
#include "core_diag.h"

static GeoVector ai_node_knowledge_src_vec(const AssetKnowledgeSourceVector* src) {
  return geo_vector(src->x, src->y, src->z, src->w);
}

static bool ai_node_knowledge_equal(const AssetBehaviorKnowledgeCompare* comp, AiBlackboard* bb) {
  // TODO: Keys should be pre-hashed in the behavior asset.
  const StringHash            keyHash = string_hash(comp->key);
  const AssetKnowledgeSource* src     = &comp->value;

  switch (src->type) {
  case AssetKnowledgeSource_Number:
    return ai_blackboard_equals_f64(bb, keyHash, src->data_number.value);
  case AssetKnowledgeSource_Bool:
    return ai_blackboard_equals_bool(bb, keyHash, src->data_bool.value);
  case AssetKnowledgeSource_Vector:
    return ai_blackboard_equals_vector(bb, keyHash, ai_node_knowledge_src_vec(&src->data_vector));
  case AssetKnowledgeSource_Knowledge:
    // TODO: Keys should be pre-hashed in the behavior asset.
    return ai_blackboard_equals(bb, keyHash, string_hash(src->data_knowledge.key));
  }
  UNREACHABLE
}

AiResult ai_node_knowledgecompare_eval(const AssetBehavior* behavior, AiBlackboard* bb) {
  diag_assert(behavior->type == AssetBehavior_KnowledgeCompare);

  switch (behavior->data_knowledgecompare.comparison) {
  case AssetKnowledgeComparison_Equal:
    return ai_node_knowledge_equal(&behavior->data_knowledgecompare, bb) ? AiResult_Success
                                                                         : AiResult_Failure;
  }
  UNREACHABLE
}
