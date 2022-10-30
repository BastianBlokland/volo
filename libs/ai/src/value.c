#include "ai_value.h"
#include "core_array.h"
#include "core_diag.h"
#include "core_math.h"
#include "core_time.h"

/**
 * AiValue's are 128bit values with 128bit alignment.
 *
 * Values are stored using a simple scheme where the type-tag is stored after the value payload.
 * In the future more compact representations can be explored.
 *
 * | Type    | Word 0        | Word 1        | Word 2     | Word 3       |
 * |---------|---------------|---------------|------------|--------------|
 * | null    | unused        | unused        | unused     | type tag (0) |
 * | f64     | lower 32 bits | upper 32 bits | unused     | type tag (1) |
 * | Bool    | 0 / 1         | unused        | unused     | type tag (2) |
 * | Vector3 | f32 x         | f32 y         | f32 z      | type tag (3) |
 * | Entity  | lower 32 bits | upper 32 bits | unused     | type tag (4) |
 *
 * NOTE: Assumes little-endian byte order.
 */

INLINE_HINT static f64 val_as_f64(const AiValue value) { return *((f64*)&value.data); }

INLINE_HINT static bool val_as_bool(const AiValue value) { return *((bool*)&value.data); }

INLINE_HINT static GeoVector val_as_vector3_dirty_w(const AiValue value) {
  return *((GeoVector*)&value.data);
}

INLINE_HINT static GeoVector val_as_vector3(const AiValue value) {
  GeoVector result = val_as_vector3_dirty_w(value);
  result.w         = 0.0f; // W value is aliased with the type tag.
  return result;
}

INLINE_HINT static EcsEntityId val_as_entity(const AiValue value) {
  return *((EcsEntityId*)&value.data);
}

AiValueType ai_value_type(const AiValue value) { return (AiValueType)value.data[3]; }

AiValue ai_value_null() {
  ASSERT(AiValueType_Null == 0, "ValueTypeNull should be initializable using zero-init");
  return (AiValue){0};
}

AiValue ai_value_f64(const f64 value) {
  AiValue result;
  *((f64*)&result.data) = value;
  result.data[3]        = AiValueType_f64;
  return result;
}

AiValue ai_value_bool(const bool value) {
  AiValue result;
  *((bool*)&result.data) = value;
  result.data[3]         = AiValueType_Bool;
  return result;
}

AiValue ai_value_vector3(const GeoVector value) {
  AiValue result;
  *((GeoVector*)&result.data) = value;
  result.data[3]              = AiValueType_Vector3;
  return result;
}

AiValue ai_value_entity(const EcsEntityId value) {
  AiValue result;
  *((EcsEntityId*)&result.data) = value;
  result.data[3]                = AiValueType_Entity;
  return result;
}

AiValue ai_value_time(const TimeDuration value) { return ai_value_f64(value / (f64)time_second); }

f64 ai_value_get_f64(const AiValue value, const f64 fallback) {
  return ai_value_type(value) == AiValueType_f64 ? val_as_f64(value) : fallback;
}

bool ai_value_get_bool(const AiValue value, const bool fallback) {
  return ai_value_type(value) == AiValueType_Bool ? val_as_bool(value) : fallback;
}

GeoVector ai_value_get_vector3(const AiValue value, const GeoVector fallback) {
  return ai_value_type(value) == AiValueType_Vector3 ? val_as_vector3(value) : fallback;
}

EcsEntityId ai_value_get_entity(const AiValue value, const EcsEntityId fallback) {
  return ai_value_type(value) == AiValueType_Entity ? val_as_entity(value) : fallback;
}

TimeDuration ai_value_get_time(const AiValue value, const TimeDuration fallback) {
  return ai_value_type(value) == AiValueType_f64 ? (TimeDuration)time_seconds(val_as_f64(value))
                                                 : fallback;
}

bool ai_value_has(const AiValue value) { return ai_value_type(value) != AiValueType_Null; }

AiValue ai_value_or(const AiValue value, const AiValue fallback) {
  return ai_value_type(value) ? value : fallback;
}

String ai_value_type_str(const AiValueType type) {
  diag_assert_msg(type < AiValueType_Count, "Invalid ai value type: {}", fmt_int(type));
  static const String g_names[] = {
      string_static("null"),
      string_static("f64"),
      string_static("bool"),
      string_static("vector3"),
      string_static("entity"),
  };
  ASSERT(array_elems(g_names) == AiValueType_Count, "Incorrect number of names");
  return g_names[type];
}

String ai_value_str_scratch(AiValue value) {
  switch (ai_value_type(value)) {
  case AiValueType_Null:
    return string_lit("null");
  case AiValueType_f64:
    return fmt_write_scratch("{}", fmt_float(val_as_f64(value)));
  case AiValueType_Bool:
    return fmt_write_scratch("{}", fmt_bool(val_as_bool(value)));
  case AiValueType_Vector3: {
    const GeoVector v = val_as_vector3_dirty_w(value);
    return fmt_write_scratch("{}", fmt_list_lit(fmt_float(v.x), fmt_float(v.y), fmt_float(v.z)));
  }
  case AiValueType_Entity:
    return fmt_write_scratch("{}", fmt_int(val_as_entity(value), .base = 16));
  case AiValueType_Count:
    break;
  }
  diag_assert_fail("Invalid ai-value");
  UNREACHABLE
}

bool ai_value_equal(AiValue a, AiValue b) {
  if (ai_value_type(a) != ai_value_type(b)) {
    return false;
  }
  static const f32 g_scalarThreshold = 1e-6f;
  static const f32 g_vectorThreshold = 1e-6f;
  switch (ai_value_type(a)) {
  case AiValueType_Null:
    return true;
  case AiValueType_f64:
    return math_abs(val_as_f64(a) - val_as_f64(b)) < g_scalarThreshold;
  case AiValueType_Bool:
    return val_as_bool(a) == val_as_bool(b);
  case AiValueType_Vector3: {
    const GeoVector vecA = val_as_vector3_dirty_w(a);
    const GeoVector vecB = val_as_vector3_dirty_w(b);
    return geo_vector_equal3(vecA, vecB, g_vectorThreshold);
  }
  case AiValueType_Entity:
    return val_as_entity(a) == val_as_entity(b);
  case AiValueType_Count:
    break;
  }
  diag_assert_fail("Invalid ai-value");
  UNREACHABLE
}

bool ai_value_less(AiValue a, AiValue b) {
  if (ai_value_type(a) != ai_value_type(b)) {
    return false; // TODO: Can we define meaningful 'less' semantics for mismatching types?
  }
  switch (ai_value_type(a)) {
  case AiValueType_Null:
    return false;
  case AiValueType_f64:
    return val_as_f64(a) < val_as_f64(b);
  case AiValueType_Bool:
    return val_as_bool(a) < val_as_bool(b); // NOTE: Questionable usefulness?
  case AiValueType_Vector3:
    return geo_vector_mag(val_as_vector3(a)) < geo_vector_mag(val_as_vector3(b));
  case AiValueType_Entity:
    return ecs_entity_id_serial(val_as_entity(a)) < ecs_entity_id_serial(val_as_entity(b));
  case AiValueType_Count:
    break;
  }
  diag_assert_fail("Invalid ai-value");
  UNREACHABLE
}

bool ai_value_greater(AiValue a, AiValue b) {
  if (ai_value_type(a) != ai_value_type(b)) {
    return false; // TODO: Can we define meaningful 'greater' semantics for mismatching types?
  }
  switch (ai_value_type(a)) {
  case AiValueType_Null:
    return false;
  case AiValueType_f64:
    return val_as_f64(a) > val_as_f64(b);
  case AiValueType_Bool:
    return val_as_bool(a) > val_as_bool(b);
  case AiValueType_Vector3:
    return geo_vector_mag(val_as_vector3(a)) > geo_vector_mag(val_as_vector3(b));
  case AiValueType_Entity:
    return ecs_entity_id_serial(val_as_entity(a)) > ecs_entity_id_serial(val_as_entity(b));
  case AiValueType_Count:
    break;
  }
  diag_assert_fail("Invalid ai-value");
  UNREACHABLE
}

AiValue ai_value_add(const AiValue a, const AiValue b) {
  if (ai_value_type(a) == AiValueType_Null) {
    return b;
  }
  if (ai_value_type(b) == AiValueType_Null) {
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
  case AiValueType_Vector3: {
    const GeoVector vecA = val_as_vector3_dirty_w(a);
    const GeoVector vecB = val_as_vector3_dirty_w(b);
    return ai_value_vector3(geo_vector_add(vecA, vecB));
  }
  case AiValueType_Entity:
    return a; // Arithmetic on entities not supported.
  case AiValueType_Null:
  case AiValueType_Count:
    break;
  }
  diag_assert_fail("Invalid ai-value");
  UNREACHABLE
}

AiValue ai_value_sub(const AiValue a, const AiValue b) {
  if (ai_value_type(a) == AiValueType_Null) {
    return b;
  }
  if (ai_value_type(b) == AiValueType_Null) {
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
  case AiValueType_Vector3: {
    const GeoVector vecA = val_as_vector3_dirty_w(a);
    const GeoVector vecB = val_as_vector3_dirty_w(b);
    return ai_value_vector3(geo_vector_sub(vecA, vecB));
  }
  case AiValueType_Entity:
    return a; // Arithmetic on entities not supported.
  case AiValueType_Null:
  case AiValueType_Count:
    break;
  }
  diag_assert_fail("Invalid ai-value");
  UNREACHABLE
}
