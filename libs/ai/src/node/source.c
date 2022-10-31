#include "core_time.h"

#include "source_internal.h"

ScriptVal ai_source_value(const AssetAiSource* src, const ScriptMem* m) {
  switch (src->type) {
  case AssetAiSource_Null: {
    return script_null();
  }
  case AssetAiSource_Number: {
    return script_number(src->data_number.value);
  }
  case AssetAiSource_Bool: {
    return script_bool(src->data_bool.value);
  }
  case AssetAiSource_Vector: {
    const AssetAiSourceVector* vecSrc = &src->data_vector;
    return script_vector3(geo_vector(vecSrc->x, vecSrc->y, vecSrc->z));
  }
  case AssetAiSource_Time: {
    static StringHash g_timeNowHash;
    if (UNLIKELY(!g_timeNowHash)) {
      g_timeNowHash = string_hash_lit("global-time");
    }
    const ScriptVal now    = script_mem_get(m, g_timeNowHash);
    const ScriptVal offset = script_time((TimeDuration)time_seconds(src->data_time.secondsFromNow));
    return script_val_add(now, offset);
  }
  case AssetAiSource_Knowledge: {
    return script_mem_get(m, src->data_knowledge.key);
  }
  }
  return script_null();
}
