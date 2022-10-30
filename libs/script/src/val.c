#include "core_array.h"
#include "core_diag.h"
#include "core_math.h"
#include "core_time.h"
#include "script_val.h"

/**
 * ScriptVal's are 128bit values with 128bit alignment.
 *
 * Values are stored using a simple scheme where the type-tag is stored after the value payload.
 * In the future more compact representations can be explored.
 *
 * | Type    | Word 0        | Word 1        | Word 2     | Word 3       |
 * |---------|---------------|---------------|------------|--------------|
 * | null    | unused        | unused        | unused     | type tag (0) |
 * | number  | lower 32 bits | upper 32 bits | unused     | type tag (1) |
 * | Bool    | 0 / 1         | unused        | unused     | type tag (2) |
 * | Vector3 | f32 x         | f32 y         | f32 z      | type tag (3) |
 * | Entity  | lower 32 bits | upper 32 bits | unused     | type tag (4) |
 *
 * NOTE: Assumes little-endian byte order.
 */

INLINE_HINT static f64 val_as_number(const ScriptVal value) { return *((f64*)&value.data); }

INLINE_HINT static bool val_as_bool(const ScriptVal value) { return *((bool*)&value.data); }

INLINE_HINT static GeoVector val_as_vector3_dirty_w(const ScriptVal value) {
  return *((GeoVector*)&value.data);
}

INLINE_HINT static GeoVector val_as_vector3(const ScriptVal value) {
  GeoVector result = val_as_vector3_dirty_w(value);
  result.w         = 0.0f; // W value is aliased with the type tag.
  return result;
}

INLINE_HINT static EcsEntityId val_as_entity(const ScriptVal value) {
  return *((EcsEntityId*)&value.data);
}

ScriptValType script_val_type(const ScriptVal value) { return (ScriptValType)value.data[3]; }

ScriptVal script_val_null() {
  ASSERT(ScriptValType_Null == 0, "ScriptValType_Null should be initializable using zero-init");
  return (ScriptVal){0};
}

ScriptVal script_val_number(const f64 value) {
  ScriptVal result;
  *((f64*)&result.data) = value;
  result.data[3]        = ScriptValType_Number;
  return result;
}

ScriptVal script_val_bool(const bool value) {
  ScriptVal result;
  *((bool*)&result.data) = value;
  result.data[3]         = ScriptValType_Bool;
  return result;
}

ScriptVal script_val_vector3(const GeoVector value) {
  ScriptVal result;
  *((GeoVector*)&result.data) = value;
  result.data[3]              = ScriptValType_Vector3;
  return result;
}

ScriptVal script_val_entity(const EcsEntityId value) {
  ScriptVal result;
  *((EcsEntityId*)&result.data) = value;
  result.data[3]                = ScriptValType_Entity;
  return result;
}

ScriptVal script_val_time(const TimeDuration value) {
  return script_val_number(value / (f64)time_second);
}

f64 script_val_get_number(const ScriptVal value, const f64 fallback) {
  return script_val_type(value) == ScriptValType_Number ? val_as_number(value) : fallback;
}

bool script_val_get_bool(const ScriptVal value, const bool fallback) {
  return script_val_type(value) == ScriptValType_Bool ? val_as_bool(value) : fallback;
}

GeoVector script_val_get_vector3(const ScriptVal value, const GeoVector fallback) {
  return script_val_type(value) == ScriptValType_Vector3 ? val_as_vector3(value) : fallback;
}

EcsEntityId script_val_get_entity(const ScriptVal value, const EcsEntityId fallback) {
  return script_val_type(value) == ScriptValType_Entity ? val_as_entity(value) : fallback;
}

TimeDuration script_val_get_time(const ScriptVal value, const TimeDuration fallback) {
  return script_val_type(value) == ScriptValType_Number
             ? (TimeDuration)time_seconds(val_as_number(value))
             : fallback;
}

bool script_val_has(const ScriptVal value) { return script_val_type(value) != ScriptValType_Null; }

ScriptVal script_val_or(const ScriptVal value, const ScriptVal fallback) {
  return script_val_type(value) ? value : fallback;
}

String script_val_type_str(const ScriptValType type) {
  diag_assert_msg(type < ScriptValType_Count, "Invalid script value type: {}", fmt_int(type));
  static const String g_names[] = {
      string_static("null"),
      string_static("number"),
      string_static("bool"),
      string_static("vector3"),
      string_static("entity"),
  };
  ASSERT(array_elems(g_names) == ScriptValType_Count, "Incorrect number of names");
  return g_names[type];
}

String script_val_str_scratch(ScriptVal value) {
  switch (script_val_type(value)) {
  case ScriptValType_Null:
    return string_lit("null");
  case ScriptValType_Number:
    return fmt_write_scratch("{}", fmt_float(val_as_number(value)));
  case ScriptValType_Bool:
    return fmt_write_scratch("{}", fmt_bool(val_as_bool(value)));
  case ScriptValType_Vector3: {
    const GeoVector v = val_as_vector3_dirty_w(value);
    return fmt_write_scratch("{}", fmt_list_lit(fmt_float(v.x), fmt_float(v.y), fmt_float(v.z)));
  }
  case ScriptValType_Entity:
    return fmt_write_scratch("{}", fmt_int(val_as_entity(value), .base = 16));
  case ScriptValType_Count:
    break;
  }
  diag_assert_fail("Invalid script value");
  UNREACHABLE
}

bool script_val_equal(ScriptVal a, ScriptVal b) {
  if (script_val_type(a) != script_val_type(b)) {
    return false;
  }
  static const f32 g_scalarThreshold = 1e-6f;
  static const f32 g_vectorThreshold = 1e-6f;
  switch (script_val_type(a)) {
  case ScriptValType_Null:
    return true;
  case ScriptValType_Number:
    return math_abs(val_as_number(a) - val_as_number(b)) < g_scalarThreshold;
  case ScriptValType_Bool:
    return val_as_bool(a) == val_as_bool(b);
  case ScriptValType_Vector3: {
    const GeoVector vecA = val_as_vector3_dirty_w(a);
    const GeoVector vecB = val_as_vector3_dirty_w(b);
    return geo_vector_equal3(vecA, vecB, g_vectorThreshold);
  }
  case ScriptValType_Entity:
    return val_as_entity(a) == val_as_entity(b);
  case ScriptValType_Count:
    break;
  }
  diag_assert_fail("Invalid script value");
  UNREACHABLE
}

bool script_val_less(ScriptVal a, ScriptVal b) {
  if (script_val_type(a) != script_val_type(b)) {
    return false; // TODO: Can we define meaningful 'less' semantics for mismatching types?
  }
  switch (script_val_type(a)) {
  case ScriptValType_Null:
    return false;
  case ScriptValType_Number:
    return val_as_number(a) < val_as_number(b);
  case ScriptValType_Bool:
    return val_as_bool(a) < val_as_bool(b); // NOTE: Questionable usefulness?
  case ScriptValType_Vector3:
    return geo_vector_mag(val_as_vector3(a)) < geo_vector_mag(val_as_vector3(b));
  case ScriptValType_Entity:
    return ecs_entity_id_serial(val_as_entity(a)) < ecs_entity_id_serial(val_as_entity(b));
  case ScriptValType_Count:
    break;
  }
  diag_assert_fail("Invalid script value");
  UNREACHABLE
}

bool script_val_greater(ScriptVal a, ScriptVal b) {
  if (script_val_type(a) != script_val_type(b)) {
    return false; // TODO: Can we define meaningful 'greater' semantics for mismatching types?
  }
  switch (script_val_type(a)) {
  case ScriptValType_Null:
    return false;
  case ScriptValType_Number:
    return val_as_number(a) > val_as_number(b);
  case ScriptValType_Bool:
    return val_as_bool(a) > val_as_bool(b);
  case ScriptValType_Vector3:
    return geo_vector_mag(val_as_vector3(a)) > geo_vector_mag(val_as_vector3(b));
  case ScriptValType_Entity:
    return ecs_entity_id_serial(val_as_entity(a)) > ecs_entity_id_serial(val_as_entity(b));
  case ScriptValType_Count:
    break;
  }
  diag_assert_fail("Invalid script value");
  UNREACHABLE
}

ScriptVal script_val_add(const ScriptVal a, const ScriptVal b) {
  if (script_val_type(a) == ScriptValType_Null) {
    return b;
  }
  if (script_val_type(b) == ScriptValType_Null) {
    return a;
  }
  if (script_val_type(a) != script_val_type(b)) {
    return a; // Arithmetic on mismatched types not supported atm.
  }
  switch (script_val_type(a)) {
  case ScriptValType_Number:
    return script_val_number(val_as_number(a) + val_as_number(b));
  case ScriptValType_Bool:
    return a; // Arithmetic on booleans not supported.
  case ScriptValType_Vector3: {
    const GeoVector vecA = val_as_vector3_dirty_w(a);
    const GeoVector vecB = val_as_vector3_dirty_w(b);
    return script_val_vector3(geo_vector_add(vecA, vecB));
  }
  case ScriptValType_Entity:
    return a; // Arithmetic on entities not supported.
  case ScriptValType_Null:
  case ScriptValType_Count:
    break;
  }
  diag_assert_fail("Invalid script value");
  UNREACHABLE
}

ScriptVal script_val_sub(const ScriptVal a, const ScriptVal b) {
  if (script_val_type(a) == ScriptValType_Null) {
    return b;
  }
  if (script_val_type(b) == ScriptValType_Null) {
    return a;
  }
  if (script_val_type(a) != script_val_type(b)) {
    return a; // Arithmetic on mismatched types not supported atm.
  }
  switch (script_val_type(a)) {
  case ScriptValType_Number:
    return script_val_number(val_as_number(a) - val_as_number(b));
  case ScriptValType_Bool:
    return a; // Arithmetic on booleans not supported.
  case ScriptValType_Vector3: {
    const GeoVector vecA = val_as_vector3_dirty_w(a);
    const GeoVector vecB = val_as_vector3_dirty_w(b);
    return script_val_vector3(geo_vector_sub(vecA, vecB));
  }
  case ScriptValType_Entity:
    return a; // Arithmetic on entities not supported.
  case ScriptValType_Null:
  case ScriptValType_Count:
    break;
  }
  diag_assert_fail("Invalid script value");
  UNREACHABLE
}
