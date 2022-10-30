#include "core_time.h"

#include "source_internal.h"

AiValue ai_source_value(const AssetAiSource* src, const AiBlackboard* bb) {
  switch (src->type) {
  case AssetAiSource_None: {
    return ai_value_null();
  }
  case AssetAiSource_Number: {
    return ai_value_f64(src->data_number.value);
  }
  case AssetAiSource_Bool: {
    return ai_value_bool(src->data_bool.value);
  }
  case AssetAiSource_Vector: {
    const AssetAiSourceVector* vecSrc = &src->data_vector;
    return ai_value_vector3(geo_vector(vecSrc->x, vecSrc->y, vecSrc->z));
  }
  case AssetAiSource_Time: {
    static StringHash g_timeNowHash;
    if (UNLIKELY(!g_timeNowHash)) {
      g_timeNowHash = string_hash_lit("global-time");
    }
    const AiValue now    = ai_blackboard_get(bb, g_timeNowHash);
    const AiValue offset = ai_value_time((TimeDuration)time_seconds(src->data_time.secondsFromNow));
    return ai_value_add(now, offset);
  }
  case AssetAiSource_Knowledge: {
    return ai_blackboard_get(bb, src->data_knowledge.key);
  }
  }
  return ai_value_null();
}
