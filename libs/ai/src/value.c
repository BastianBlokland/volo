#include "ai_value.h"
#include "core_array.h"
#include "core_diag.h"
#include "core_math.h"
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

String ai_value_str_scratch(const AiValue* value) {
  switch (value->type) {
  case AiValueType_f64:
    return fmt_write_scratch("{}", fmt_float(value->data_f64));
  case AiValueType_Bool:
    return fmt_write_scratch("{}", fmt_bool(value->data_bool));
  case AiValueType_Vector:
    return fmt_write_scratch("{}", geo_vector_fmt(value->data_vector));
  case AiValueType_Time:
    return fmt_write_scratch("{}", fmt_duration(value->data_time));
  case AiValueType_Entity:
    return fmt_write_scratch("{}", fmt_int(value->data_entity, .base = 16));
  case AiValueType_Count:
    break;
  }
  UNREACHABLE
}

bool ai_value_equal(const AiValue* a, const AiValue* b) {
  if (a->type != b->type) {
    return false;
  }
  static const f32 g_scalarThreshold = 1e-6f;
  static const f32 g_vectorThreshold = 1e-6f;
  switch (a->type) {
  case AiValueType_f64:
    return math_abs(a->data_f64 - b->data_f64) < g_scalarThreshold;
  case AiValueType_Bool:
    return a->data_bool == b->data_bool;
  case AiValueType_Vector:
    return geo_vector_equal(a->data_vector, b->data_vector, g_vectorThreshold);
  case AiValueType_Time:
    return a->data_time == b->data_time;
  case AiValueType_Entity:
    return a->data_entity == b->data_entity;
  case AiValueType_Count:
    break;
  }
  UNREACHABLE
}

bool ai_value_less(const AiValue* a, const AiValue* b) {
  if (a->type != b->type) {
    return false; // TODO: Can we define meaningful 'less' semantics for mismatching types?
  }
  switch (a->type) {
  case AiValueType_f64:
    return a->data_f64 < b->data_f64;
  case AiValueType_Bool:
    return a->data_bool < b->data_bool; // NOTE: Questionable usefulness?
  case AiValueType_Vector:
    return geo_vector_mag(a->data_vector) < geo_vector_mag(b->data_vector);
  case AiValueType_Time:
    return a->data_time < b->data_time;
  case AiValueType_Entity:
    return ecs_entity_id_serial(a->data_entity) < ecs_entity_id_serial(b->data_entity);
  case AiValueType_Count:
    break;
  }
  UNREACHABLE
}
