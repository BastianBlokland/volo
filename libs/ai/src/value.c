#include "ai_value.h"
#include "core_array.h"
#include "core_diag.h"
#include "core_time.h"

AiValue ai_value_f64(const f64 value) {
  return (AiValue){.type = AiValueType_f64, .data_f64 = value};
}

AiValue ai_value_bool(const bool value) {
  return (AiValue){.type = AiValueType_Bool, .data_bool = value};
}

AiValue ai_value_vector(const GeoVector value) {
  return (AiValue){.type = AiValueType_Vector, .data_vector = value};
}

AiValue ai_value_time(const TimeDuration value) {
  return (AiValue){.type = AiValueType_Time, .data_time = value};
}

AiValue ai_value_entity(const EcsEntityId value) {
  return (AiValue){.type = AiValueType_Entity, .data_entity = value};
}

String ai_value_type_str(const AiValueType type) {
  static const String g_names[] = {
      string_static("f64"),
      string_static("bool"),
      string_static("vector"),
      string_static("time"),
      string_static("entity"),
  };
  ASSERT(array_elems(g_names) == AiValueType_Count, "Incorrect number of names");
  return g_names[type];
}
