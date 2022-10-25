#include "ai_value.h"
#include "core_array.h"
#include "core_diag.h"
#include "core_math.h"
#include "core_time.h"

static f64          val_as_f64(const AiValue value) { return *((f64*)&value.data); }
static bool         val_as_bool(const AiValue value) { return *((bool*)&value.data); }
static GeoVector    val_as_vector(const AiValue value) { return *((GeoVector*)&value.data); }
static TimeDuration val_as_time(const AiValue value) { return *((TimeDuration*)&value.data); }
static EcsEntityId  val_as_entity(const AiValue value) { return *((EcsEntityId*)&value.data); }

AiValueType ai_value_type(const AiValue value) { return value.type; }

AiValue ai_value_none() { return (AiValue){.type = AiValueType_None}; }

AiValue ai_value_f64(const f64 value) {
  AiValue result        = (AiValue){.type = AiValueType_f64};
  *((f64*)&result.data) = value;
  return result;
}

AiValue ai_value_bool(const bool value) {
  AiValue result         = (AiValue){.type = AiValueType_Bool};
  *((bool*)&result.data) = value;
  return result;
}

AiValue ai_value_vector3(const GeoVector value) {
  AiValue result              = (AiValue){.type = AiValueType_Vector3};
  *((GeoVector*)&result.data) = value;
  return result;
}

AiValue ai_value_time(const TimeDuration value) {
  AiValue result                 = (AiValue){.type = AiValueType_Time};
  *((TimeDuration*)&result.data) = value;
  return result;
}

AiValue ai_value_entity(const EcsEntityId value) {
  AiValue result                = (AiValue){.type = AiValueType_Entity};
  *((EcsEntityId*)&result.data) = value;
  return result;
}

f64 ai_value_get_f64(const AiValue value, const f64 fallback) {
  return ai_value_type(value) == AiValueType_f64 ? val_as_f64(value) : fallback;
}

bool ai_value_get_bool(const AiValue value, const bool fallback) {
  return ai_value_type(value) == AiValueType_Bool ? val_as_bool(value) : fallback;
}

GeoVector ai_value_get_vector3(const AiValue value, const GeoVector fallback) {
  return ai_value_type(value) == AiValueType_Vector3 ? val_as_vector(value) : fallback;
}

TimeDuration ai_value_get_time(const AiValue value, const TimeDuration fallback) {
  return ai_value_type(value) == AiValueType_Time ? val_as_time(value) : fallback;
}

EcsEntityId ai_value_get_entity(const AiValue value, const EcsEntityId fallback) {
  return ai_value_type(value) == AiValueType_Entity ? val_as_entity(value) : fallback;
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
    return fmt_write_scratch("{}", fmt_float(val_as_f64(value)));
  case AiValueType_Bool:
    return fmt_write_scratch("{}", fmt_bool(val_as_bool(value)));
  case AiValueType_Vector3: {
    const GeoVector v = val_as_vector(value);
    return fmt_write_scratch("{}", fmt_list_lit(fmt_float(v.x), fmt_float(v.y), fmt_float(v.z)));
  }
  case AiValueType_Time:
    return fmt_write_scratch("{}", fmt_duration(val_as_time(value)));
  case AiValueType_Entity:
    return fmt_write_scratch("{}", fmt_int(val_as_entity(value), .base = 16));
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
    return math_abs(val_as_f64(a) - val_as_f64(b)) < g_scalarThreshold;
  case AiValueType_Bool:
    return val_as_bool(a) == val_as_bool(b);
  case AiValueType_Vector3:
    return geo_vector_equal3(val_as_vector(a), val_as_vector(b), g_vectorThreshold);
  case AiValueType_Time:
    return val_as_time(a) == val_as_time(b);
  case AiValueType_Entity:
    return val_as_entity(a) == val_as_entity(b);
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
    return val_as_f64(a) < val_as_f64(b);
  case AiValueType_Bool:
    return val_as_bool(a) < val_as_bool(b); // NOTE: Questionable usefulness?
  case AiValueType_Vector3:
    return geo_vector_mag(val_as_vector(a)) < geo_vector_mag(val_as_vector(b));
  case AiValueType_Time:
    return val_as_time(a) < val_as_time(b);
  case AiValueType_Entity:
    return ecs_entity_id_serial(val_as_entity(a)) < ecs_entity_id_serial(val_as_entity(b));
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
    return val_as_f64(a) > val_as_f64(b);
  case AiValueType_Bool:
    return val_as_bool(a) > val_as_bool(b);
  case AiValueType_Vector3:
    return geo_vector_mag(val_as_vector(a)) > geo_vector_mag(val_as_vector(b));
  case AiValueType_Time:
    return val_as_time(a) > val_as_time(b);
  case AiValueType_Entity:
    return ecs_entity_id_serial(val_as_entity(a)) > ecs_entity_id_serial(val_as_entity(b));
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
    return ai_value_f64(val_as_f64(a) + val_as_f64(b));
  case AiValueType_Bool:
    return a; // Arithmetic on booleans not supported.
  case AiValueType_Vector3:
    return ai_value_vector3(geo_vector_add(val_as_vector(a), val_as_vector(b)));
  case AiValueType_Time:
    return ai_value_time(val_as_time(a) + val_as_time(b));
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
    return ai_value_f64(val_as_f64(a) - val_as_f64(b));
  case AiValueType_Bool:
    return a; // Arithmetic on booleans not supported.
  case AiValueType_Vector3:
    return ai_value_vector3(geo_vector_sub(val_as_vector(a), val_as_vector(b)));
  case AiValueType_Time:
    return ai_value_time(val_as_time(a) - val_as_time(b));
  case AiValueType_Entity:
    return a; // Arithmetic on entities not supported.
  case AiValueType_None:
  case AiValueType_Count:
    break;
  }
  diag_assert_fail("Invalid ai-value");
  return ai_value_none();
}
