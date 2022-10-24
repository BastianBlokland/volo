#include "ai_value.h"
#include "core_array.h"
#include "core_diag.h"
#include "core_math.h"
#include "core_time.h"

AiValueType ai_value_type(const AiValue value) { return value.type; }

AiValue ai_value_none() { return (AiValue){.type = AiValueType_None}; }

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

f64 ai_value_get_f64(const AiValue value, const f64 fallback) {
  return ai_value_type(value) == AiValueType_f64 ? value.data_f64 : fallback;
}

bool ai_value_get_bool(const AiValue value, const bool fallback) {
  return ai_value_type(value) == AiValueType_Bool ? value.data_bool : fallback;
}

GeoVector ai_value_get_vector(const AiValue value, const GeoVector fallback) {
  return ai_value_type(value) == AiValueType_Vector ? value.data_vector : fallback;
}

TimeDuration ai_value_get_time(const AiValue value, const TimeDuration fallback) {
  return ai_value_type(value) == AiValueType_Time ? value.data_time : fallback;
}

EcsEntityId ai_value_get_entity(const AiValue value, const EcsEntityId fallback) {
  return ai_value_type(value) == AiValueType_Entity ? value.data_entity : fallback;
}

bool ai_value_has(const AiValue value) { return ai_value_type(value) != AiValueType_None; }

AiValue ai_value_or(const AiValue value, const AiValue fallback) {
  return ai_value_type(value) ? value : fallback;
}

String ai_value_type_str(const AiValueType type) {
  diag_assert_msg(type < AiValueType_Count, "Invalid ai value type: {}", fmt_int(type));
  static const String g_names[] = {
      string_static("none"),
      string_static("f64"),
      string_static("bool"),
      string_static("vector"),
      string_static("time"),
      string_static("entity"),
  };
  ASSERT(array_elems(g_names) == AiValueType_Count, "Incorrect number of names");
  return g_names[type];
}

String ai_value_str_scratch(AiValue value) {
  switch (ai_value_type(value)) {
  case AiValueType_None:
    return string_lit("none");
  case AiValueType_f64:
    return fmt_write_scratch("{}", fmt_float(value.data_f64));
  case AiValueType_Bool:
    return fmt_write_scratch("{}", fmt_bool(value.data_bool));
  case AiValueType_Vector:
    return fmt_write_scratch("{}", geo_vector_fmt(value.data_vector));
  case AiValueType_Time:
    return fmt_write_scratch("{}", fmt_duration(value.data_time));
  case AiValueType_Entity:
    return fmt_write_scratch("{}", fmt_int(value.data_entity, .base = 16));
  case AiValueType_Count:
    break;
  }
  diag_assert_fail("Invalid ai-value");
  return string_empty;
}

bool ai_value_equal(AiValue a, AiValue b) {
  if (ai_value_type(a) != ai_value_type(b)) {
    return false;
  }
  static const f32 g_scalarThreshold = 1e-6f;
  static const f32 g_vectorThreshold = 1e-6f;
  switch (ai_value_type(a)) {
  case AiValueType_None:
    return true;
  case AiValueType_f64:
    return math_abs(a.data_f64 - b.data_f64) < g_scalarThreshold;
  case AiValueType_Bool:
    return a.data_bool == b.data_bool;
  case AiValueType_Vector:
    return geo_vector_equal(a.data_vector, b.data_vector, g_vectorThreshold);
  case AiValueType_Time:
    return a.data_time == b.data_time;
  case AiValueType_Entity:
    return a.data_entity == b.data_entity;
  case AiValueType_Count:
    break;
  }
  diag_assert_fail("Invalid ai-value");
  return false;
}

bool ai_value_less(AiValue a, AiValue b) {
  if (ai_value_type(a) != ai_value_type(b)) {
    return false; // TODO: Can we define meaningful 'less' semantics for mismatching types?
  }
  switch (ai_value_type(a)) {
  case AiValueType_None:
    return false;
  case AiValueType_f64:
    return a.data_f64 < b.data_f64;
  case AiValueType_Bool:
    return a.data_bool < b.data_bool; // NOTE: Questionable usefulness?
  case AiValueType_Vector:
    return geo_vector_mag(a.data_vector) < geo_vector_mag(b.data_vector);
  case AiValueType_Time:
    return a.data_time < b.data_time;
  case AiValueType_Entity:
    return ecs_entity_id_serial(a.data_entity) < ecs_entity_id_serial(b.data_entity);
  case AiValueType_Count:
    break;
  }
  diag_assert_fail("Invalid ai-value");
  return false;
}

bool ai_value_greater(AiValue a, AiValue b) {
  if (ai_value_type(a) != ai_value_type(b)) {
    return false; // TODO: Can we define meaningful 'greater' semantics for mismatching types?
  }
  switch (ai_value_type(a)) {
  case AiValueType_None:
    return false;
  case AiValueType_f64:
    return a.data_f64 > b.data_f64;
  case AiValueType_Bool:
    return a.data_bool > b.data_bool;
  case AiValueType_Vector:
    return geo_vector_mag(a.data_vector) > geo_vector_mag(b.data_vector);
  case AiValueType_Time:
    return a.data_time > b.data_time;
  case AiValueType_Entity:
    return ecs_entity_id_serial(a.data_entity) > ecs_entity_id_serial(b.data_entity);
  case AiValueType_Count:
    break;
  }
  diag_assert_fail("Invalid ai-value");
  return false;
}

AiValue ai_value_add(const AiValue a, const AiValue b) {
  if (ai_value_type(a) == AiValueType_None) {
    return b;
  }
  if (ai_value_type(b) == AiValueType_None) {
    return a;
  }
  if (ai_value_type(a) != ai_value_type(b)) {
    return a; // Arithmetic on mismatched types not supported atm.
  }
  switch (ai_value_type(a)) {
  case AiValueType_f64:
    return ai_value_f64(a.data_f64 + b.data_f64);
  case AiValueType_Bool:
    return a; // Arithmetic on booleans not supported.
  case AiValueType_Vector:
    return ai_value_vector(geo_vector_add(a.data_vector, b.data_vector));
  case AiValueType_Time:
    return ai_value_time(a.data_time + b.data_time);
  case AiValueType_Entity:
    return a; // Arithmetic on entities not supported.
  case AiValueType_None:
  case AiValueType_Count:
    break;
  }
  diag_assert_fail("Invalid ai-value");
  return ai_value_none();
}

AiValue ai_value_sub(const AiValue a, const AiValue b) {
  if (ai_value_type(a) == AiValueType_None) {
    return b;
  }
  if (ai_value_type(b) == AiValueType_None) {
    return a;
  }
  if (ai_value_type(a) != ai_value_type(b)) {
    return a; // Arithmetic on mismatched types not supported atm.
  }
  switch (ai_value_type(a)) {
  case AiValueType_f64:
    return ai_value_f64(a.data_f64 - b.data_f64);
  case AiValueType_Bool:
    return a; // Arithmetic on booleans not supported.
  case AiValueType_Vector:
    return ai_value_vector(geo_vector_sub(a.data_vector, b.data_vector));
  case AiValueType_Time:
    return ai_value_time(a.data_time - b.data_time);
  case AiValueType_Entity:
    return a; // Arithmetic on entities not supported.
  case AiValueType_None:
  case AiValueType_Count:
    break;
  }
  diag_assert_fail("Invalid ai-value");
  return ai_value_none();
}
