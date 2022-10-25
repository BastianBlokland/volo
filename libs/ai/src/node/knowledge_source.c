#include "core_time.h"

#include "knowledge_source_internal.h"

AiValue ai_knowledge_source_value(const AssetKnowledgeSource* src, const AiBlackboard* bb) {
  switch (src->type) {
  case AssetKnowledgeSource_Number: {
    return ai_value_f64(src->data_number.value);
  }
  case AssetKnowledgeSource_Bool: {
    return ai_value_bool(src->data_bool.value);
  }
  case AssetKnowledgeSource_Vector: {
    const AssetKnowledgeSourceVector* vecSrc = &src->data_vector;
    return ai_value_vector3(geo_vector(vecSrc->x, vecSrc->y, vecSrc->z));
  }
  case AssetKnowledgeSource_Time: {
    static StringHash g_timeNowHash;
    if (UNLIKELY(!g_timeNowHash)) {
      g_timeNowHash = string_hash_lit("global-time");
    }
    const AiValue now    = ai_blackboard_get(bb, g_timeNowHash);
    const AiValue offset = ai_value_time((TimeDuration)time_seconds(src->data_time.secondsFromNow));
    return ai_value_add(now, offset);
  }
  case AssetKnowledgeSource_Knowledge: {
    // TODO: Keys should be pre-hashed in the behavior asset.
    const StringHash srcKeyHash = string_hash(src->data_knowledge.key);
    return ai_blackboard_get(bb, srcKeyHash);
  }
  }
  return ai_value_none();
}
