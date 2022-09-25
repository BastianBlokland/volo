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

static bool ai_node_knowledge_less(const AssetBehaviorKnowledgeCompare* comp, AiBlackboard* bb) {
  // TODO: Keys should be pre-hashed in the behavior asset.
  const StringHash            keyHash = string_hash(comp->key);
  const AssetKnowledgeSource* src     = &comp->value;

  /**
   * NOTE: Less comparision is only implemented for f64 values at this time.
   */

  switch (src->type) {
  case AssetKnowledgeSource_Number:
    return ai_blackboard_less_f64(bb, keyHash, src->data_number.value);
  case AssetKnowledgeSource_Bool:
  case AssetKnowledgeSource_Vector:
    return false;
  case AssetKnowledgeSource_Knowledge: {
    // TODO: Keys should be pre-hashed in the behavior asset.
    const StringHash valueKeyHash = string_hash(src->data_knowledge.key);
    if (ai_blackboard_type(bb, valueKeyHash) != AiBlackboardType_f64) {
      return false;
    }
    return ai_blackboard_less_f64(bb, keyHash, ai_blackboard_get_f64(bb, valueKeyHash));
  }
  }
  UNREACHABLE
}

AiResult
ai_node_knowledgecompare_eval(const AssetBehavior* behavior, AiBlackboard* bb, AiTracer* tracer) {
  diag_assert(behavior->type == AssetBehavior_KnowledgeCompare);
  (void)tracer;

  bool result;
  switch (behavior->data_knowledgecompare.comparison) {
  case AssetKnowledgeComparison_Equal:
    result = ai_node_knowledge_equal(&behavior->data_knowledgecompare, bb);
    break;
  case AssetKnowledgeComparison_Less:
    result = ai_node_knowledge_less(&behavior->data_knowledgecompare, bb);
    break;
  }
  return result ? AiResult_Success : AiResult_Failure;
}
