#include "core_alloc.h"
#include "core_array.h"
#include "core_diag.h"
#include "core_float.h"
#include "core_intrinsic.h"
#include "core_math.h"
#include "core_rng.h"
#include "core_stringtable.h"
#include "core_thread.h"
#include "core_time.h"
#include "script_val.h"

/**
 * ScriptVal's are 128bit values with 128bit alignment.
 *
 * | Type    | Word 0        | Word 1        | Word 2     | Word 3       |
 * |---------|---------------|---------------|------------|--------------|
 * | null    | unused        | unused        | unused     | type tag (0) |
 * | number  | lower 32 bits | upper 32 bits | unused     | type tag (1) |
 * | Bool    | 0 / 1         | unused        | unused     | type tag (2) |
 * | Vector3 | f32 x         | f32 y         | f32 z      | type tag (3) |
 * | Entity  | lower 32 bits | upper 32 bits | unused     | type tag (4) |
 * | String  | u32           | unused        | unused     | type tag (5) |
 *
 * NOTE: Assumes little-endian byte order.
 */

INLINE_HINT static f64 val_as_number(const ScriptVal value) { return value.unsafeNumber; }

INLINE_HINT static bool val_as_bool(const ScriptVal value) { return value.unsafeBool; }

INLINE_HINT static GeoVector val_as_vector3_dirty_w(const ScriptVal value) {
  return value.unsafeVector;
}

INLINE_HINT static GeoVector val_as_vector3(const ScriptVal value) {
  GeoVector result = val_as_vector3_dirty_w(value);
  result.w         = 0.0f; // W value is aliased with the type tag.
  return result;
}

INLINE_HINT static EcsEntityId val_as_entity(const ScriptVal value) { return value.unsafeEntity; }

INLINE_HINT static StringHash val_as_string(const ScriptVal value) {
  return value.unsafeStringHash;
}

ScriptType script_type(const ScriptVal value) { return (ScriptType)value.data[3]; }

ScriptVal script_null() {
  ASSERT(ScriptType_Null == 0, "ScriptType_Null should be initializable using zero-init");
  return (ScriptVal){0};
}

ScriptVal script_number(const f64 value) {
  ScriptVal result;
  result.unsafeNumber = value;
  result.data[3]      = ScriptType_Number;
  return result;
}

ScriptVal script_bool(const bool value) {
  ScriptVal result;
  result.unsafeBool = value;
  result.data[3]    = ScriptType_Bool;
  return result;
}

ScriptVal script_vector3(const GeoVector value) {
  ScriptVal result;
  result.unsafeVector = value;
  result.data[3]      = ScriptType_Vector3;
  return result;
}

ScriptVal script_vector3_lit(const f32 x, const f32 y, const f32 z) {
  ScriptVal result;
  result.unsafeVector = geo_vector(x, y, z);
  result.data[3]      = ScriptType_Vector3;
  return result;
}

ScriptVal script_entity(const EcsEntityId value) {
  ScriptVal result;
  result.unsafeEntity = value;
  result.data[3]      = ScriptType_Entity;
  return result;
}

ScriptVal script_entity_or_null(const EcsEntityId entity) {
  return entity ? script_entity(entity) : script_null();
}

ScriptVal script_string(const StringHash str) {
  ScriptVal result;
  result.unsafeStringHash = str;
  result.data[3]          = ScriptType_String;
  return result;
}

ScriptVal script_time(const TimeDuration value) { return script_number(value / (f64)time_second); }

f64 script_get_number(const ScriptVal value, const f64 fallback) {
  return script_type(value) == ScriptType_Number ? val_as_number(value) : fallback;
}

bool script_get_bool(const ScriptVal value, const bool fallback) {
  return script_type(value) == ScriptType_Bool ? val_as_bool(value) : fallback;
}

GeoVector script_get_vector3(const ScriptVal value, const GeoVector fallback) {
  return script_type(value) == ScriptType_Vector3 ? val_as_vector3(value) : fallback;
}

EcsEntityId script_get_entity(const ScriptVal value, const EcsEntityId fallback) {
  return script_type(value) == ScriptType_Entity ? val_as_entity(value) : fallback;
}

StringHash script_get_string(const ScriptVal value, const StringHash fallback) {
  return script_type(value) == ScriptType_String ? val_as_string(value) : fallback;
}

TimeDuration script_get_time(const ScriptVal value, const TimeDuration fallback) {
  return script_type(value) == ScriptType_Number ? (TimeDuration)time_seconds(val_as_number(value))
                                                 : fallback;
}

bool script_truthy(const ScriptVal value) {
  switch (script_type(value)) {
  case ScriptType_Null:
    return false;
  case ScriptType_Number:
    return val_as_number(value) != 0;
  case ScriptType_Bool:
    return val_as_bool(value);
  case ScriptType_Vector3:
    return geo_vector_mag_sqr(val_as_vector3(value)) > f32_epsilon;
  case ScriptType_Entity:
    return ecs_entity_valid(val_as_entity(value));
  case ScriptType_String:
    return val_as_string(value) != 0;
  case ScriptType_Count:
    break;
  }
  diag_assert_fail("Invalid script value");
  UNREACHABLE
}

bool script_falsy(const ScriptVal value) { return !script_truthy(value); }

bool script_val_has(const ScriptVal value) { return script_type(value) != ScriptType_Null; }

ScriptVal script_val_or(const ScriptVal value, const ScriptVal fallback) {
  return script_type(value) ? value : fallback;
}

String script_val_type_str(const ScriptType type) {
  diag_assert_msg(type < ScriptType_Count, "Invalid script value type: {}", fmt_int(type));
  static const String g_names[] = {
      string_static("null"),
      string_static("number"),
      string_static("bool"),
      string_static("vector3"),
      string_static("entity"),
      string_static("string"),
  };
  ASSERT(array_elems(g_names) == ScriptType_Count, "Incorrect number of names");
  return g_names[type];
}

StringHash script_val_type_hash(const ScriptType type) {
  diag_assert_msg(type < ScriptType_Count, "Invalid script value type: {}", fmt_int(type));
  static StringHash     g_hashes[ScriptType_Count];
  static bool           g_hashesInit;
  static ThreadSpinLock g_hashesInitLock;
  if (UNLIKELY(!g_hashesInit)) {
    thread_spinlock_lock(&g_hashesInitLock);
    if (!g_hashesInit) {
      for (ScriptType t = 0; t != ScriptType_Count; ++t) {
        g_hashes[t] = stringtable_add(g_stringtable, script_val_type_str(t));
      }
      g_hashesInit = true;
    }
    thread_spinlock_unlock(&g_hashesInitLock);
  }
  return g_hashes[type];
}

void script_val_str_write(const ScriptVal value, DynString* str) {
  switch (script_type(value)) {
  case ScriptType_Null:
    dynstring_append(str, string_lit("null"));
    return;
  case ScriptType_Number:
    format_write_f64(str, val_as_number(value), &format_opts_float());
    return;
  case ScriptType_Bool:
    format_write_bool(str, val_as_bool(value));
    return;
  case ScriptType_Vector3: {
    const GeoVector v = val_as_vector3_dirty_w(value);
    format_write_arg(str, &fmt_list_lit(fmt_float(v.x), fmt_float(v.y), fmt_float(v.z)));
    return;
  }
  case ScriptType_Entity:
    format_write_u64(str, val_as_entity(value), &format_opts_int(.base = 16));
    return;
  case ScriptType_String: {
    const String valueString = stringtable_lookup(g_stringtable, val_as_string(value));
    if (string_is_empty(valueString)) {
      fmt_write(str, "#{}", fmt_int(val_as_string(value), .base = 16));
    } else {
      fmt_write(str, "\"{}\"", fmt_text(valueString));
    }
    return;
  }
  case ScriptType_Count:
    break;
  }
  diag_assert_fail("Invalid script value");
  UNREACHABLE
}

String script_val_str_scratch(const ScriptVal value) {
  const Mem scratchMem = alloc_alloc(g_alloc_scratch, 128, 1);
  DynString str        = dynstring_create_over(scratchMem);

  script_val_str_write(value, &str);

  const String res = dynstring_view(&str);
  dynstring_destroy(&str);
  return res;
}

bool script_val_equal(const ScriptVal a, const ScriptVal b) {
  if (script_type(a) != script_type(b)) {
    return false;
  }
  static const f32 g_scalarThreshold = 1e-6f;
  static const f32 g_vectorThreshold = 1e-6f;
  switch (script_type(a)) {
  case ScriptType_Null:
    return true;
  case ScriptType_Number:
    return math_abs(val_as_number(a) - val_as_number(b)) < g_scalarThreshold;
  case ScriptType_Bool:
    return val_as_bool(a) == val_as_bool(b);
  case ScriptType_Vector3: {
    const GeoVector vecA = val_as_vector3_dirty_w(a);
    const GeoVector vecB = val_as_vector3_dirty_w(b);
    return geo_vector_equal3(vecA, vecB, g_vectorThreshold);
  }
  case ScriptType_Entity:
    return val_as_entity(a) == val_as_entity(b);
  case ScriptType_String:
    return val_as_string(a) == val_as_string(b);
  case ScriptType_Count:
    break;
  }
  diag_assert_fail("Invalid script value");
  UNREACHABLE
}

bool script_val_less(const ScriptVal a, const ScriptVal b) {
  if (script_type(a) != script_type(b)) {
    return false;
  }
  switch (script_type(a)) {
  case ScriptType_Null:
  case ScriptType_String:
    return false;
  case ScriptType_Number:
    return val_as_number(a) < val_as_number(b);
  case ScriptType_Bool:
    return val_as_bool(a) < val_as_bool(b); // NOTE: Questionable usefulness?
  case ScriptType_Vector3:
    return geo_vector_mag(val_as_vector3(a)) < geo_vector_mag(val_as_vector3(b));
  case ScriptType_Entity:
    return ecs_entity_id_serial(val_as_entity(a)) < ecs_entity_id_serial(val_as_entity(b));
  case ScriptType_Count:
    break;
  }
  diag_assert_fail("Invalid script value");
  UNREACHABLE
}

bool script_val_greater(const ScriptVal a, const ScriptVal b) {
  if (script_type(a) != script_type(b)) {
    return false;
  }
  switch (script_type(a)) {
  case ScriptType_Null:
  case ScriptType_String:
    return false;
  case ScriptType_Number:
    return val_as_number(a) > val_as_number(b);
  case ScriptType_Bool:
    return val_as_bool(a) > val_as_bool(b);
  case ScriptType_Vector3:
    return geo_vector_mag(val_as_vector3(a)) > geo_vector_mag(val_as_vector3(b));
  case ScriptType_Entity:
    return ecs_entity_id_serial(val_as_entity(a)) > ecs_entity_id_serial(val_as_entity(b));
  case ScriptType_Count:
    break;
  }
  diag_assert_fail("Invalid script value");
  UNREACHABLE
}

ScriptVal script_val_neg(const ScriptVal val) {
  switch (script_type(val)) {
  case ScriptType_Null:
  case ScriptType_Bool:
  case ScriptType_Entity:
  case ScriptType_String:
    return script_null();
  case ScriptType_Number:
    return script_number(-val_as_number(val));
  case ScriptType_Vector3: {
    const GeoVector vec = val_as_vector3_dirty_w(val);
    return script_vector3(geo_vector_mul(vec, -1.0f));
  }
  case ScriptType_Count:
    break;
  }
  diag_assert_fail("Invalid script value");
  UNREACHABLE
}

ScriptVal script_val_inv(const ScriptVal val) { return script_bool(!script_truthy(val)); }

ScriptVal script_val_add(const ScriptVal a, const ScriptVal b) {
  if (script_type(a) != script_type(b)) {
    return script_null();
  }
  switch (script_type(a)) {
  case ScriptType_Null:
  case ScriptType_Bool:
  case ScriptType_Entity:
  case ScriptType_String:
    return script_null();
  case ScriptType_Number:
    return script_number(val_as_number(a) + val_as_number(b));
  case ScriptType_Vector3: {
    const GeoVector vecA = val_as_vector3_dirty_w(a);
    const GeoVector vecB = val_as_vector3_dirty_w(b);
    return script_vector3(geo_vector_add(vecA, vecB));
  }
  case ScriptType_Count:
    break;
  }
  diag_assert_fail("Invalid script value");
  UNREACHABLE
}

ScriptVal script_val_sub(const ScriptVal a, const ScriptVal b) {
  if (script_type(a) != script_type(b)) {
    return script_null();
  }
  switch (script_type(a)) {
  case ScriptType_Null:
  case ScriptType_Bool:
  case ScriptType_Entity:
  case ScriptType_String:
    return script_null();
  case ScriptType_Number:
    return script_number(val_as_number(a) - val_as_number(b));
  case ScriptType_Vector3: {
    const GeoVector vecA = val_as_vector3_dirty_w(a);
    const GeoVector vecB = val_as_vector3_dirty_w(b);
    return script_vector3(geo_vector_sub(vecA, vecB));
  }
  case ScriptType_Count:
    break;
  }
  diag_assert_fail("Invalid script value");
  UNREACHABLE
}

ScriptVal script_val_mul(const ScriptVal a, const ScriptVal b) {
  switch (script_type(a)) {
  case ScriptType_Null:
  case ScriptType_Bool:
  case ScriptType_Entity:
  case ScriptType_String:
    return script_null();
  case ScriptType_Number:
    return script_type(b) == ScriptType_Number ? script_number(val_as_number(a) * val_as_number(b))
                                               : script_null();
  case ScriptType_Vector3: {
    if (script_type(b) == ScriptType_Number) {
      const GeoVector vecA = val_as_vector3_dirty_w(a);
      return script_vector3(geo_vector_mul(vecA, (f32)val_as_number(b)));
    }
    if (script_type(b) == ScriptType_Vector3) {
      const GeoVector vecA = val_as_vector3_dirty_w(a);
      const GeoVector vecB = val_as_vector3_dirty_w(b);
      return script_vector3(geo_vector_mul_comps(vecA, vecB));
    }
    return script_null();
  }
  case ScriptType_Count:
    break;
  }
  diag_assert_fail("Invalid script value");
  UNREACHABLE
}

ScriptVal script_val_div(const ScriptVal a, const ScriptVal b) {
  switch (script_type(a)) {
  case ScriptType_Null:
  case ScriptType_Bool:
  case ScriptType_Entity:
  case ScriptType_String:
    return script_null();
  case ScriptType_Number:
    return script_type(b) == ScriptType_Number ? script_number(val_as_number(a) / val_as_number(b))
                                               : script_null();
  case ScriptType_Vector3: {
    if (script_type(b) == ScriptType_Number) {
      const GeoVector vecA = val_as_vector3_dirty_w(a);
      return script_vector3(geo_vector_div(vecA, (f32)val_as_number(b)));
    }
    if (script_type(b) == ScriptType_Vector3) {
      const GeoVector vecA = val_as_vector3_dirty_w(a);
      const GeoVector vecB = val_as_vector3_dirty_w(b);
      return script_vector3(geo_vector_div_comps(vecA, vecB));
    }
    return script_null();
  }
  case ScriptType_Count:
    break;
  }
  diag_assert_fail("Invalid script value");
  UNREACHABLE
}

ScriptVal script_val_mod(const ScriptVal a, const ScriptVal b) {
  switch (script_type(a)) {
  case ScriptType_Null:
  case ScriptType_Bool:
  case ScriptType_Entity:
  case ScriptType_String:
    return script_null();
  case ScriptType_Number:
    return script_type(b) == ScriptType_Number
               ? script_number(intrinsic_fmod_f64(val_as_number(a), val_as_number(b)))
               : script_null();
  case ScriptType_Vector3: {
    if (script_type(b) == ScriptType_Number) {
      const GeoVector vecA    = val_as_vector3_dirty_w(a);
      const f32       scalarB = (f32)val_as_number(b);
      return script_vector3_lit(
          intrinsic_fmod_f32(vecA.x, scalarB),
          intrinsic_fmod_f32(vecA.y, scalarB),
          intrinsic_fmod_f32(vecA.z, scalarB));
    }
    if (script_type(b) == ScriptType_Vector3) {
      const GeoVector vecA = val_as_vector3_dirty_w(a);
      const GeoVector vecB = val_as_vector3_dirty_w(b);
      return script_vector3_lit(
          intrinsic_fmod_f32(vecA.x, vecB.x),
          intrinsic_fmod_f32(vecA.y, vecB.y),
          intrinsic_fmod_f32(vecA.z, vecB.z));
    }
    return script_null();
  }
  case ScriptType_Count:
    break;
  }
  diag_assert_fail("Invalid script value");
  UNREACHABLE
}

ScriptVal script_val_dist(const ScriptVal a, const ScriptVal b) {
  if (script_type(a) != script_type(b)) {
    return script_null();
  }
  switch (script_type(a)) {
  case ScriptType_Null:
  case ScriptType_Bool:
  case ScriptType_Entity:
  case ScriptType_String:
    return script_null();
  case ScriptType_Number:
    return script_number(math_abs(val_as_number(a) - val_as_number(b)));
  case ScriptType_Vector3: {
    const GeoVector vecA = val_as_vector3_dirty_w(a);
    const GeoVector vecB = val_as_vector3_dirty_w(b);
    return script_number(geo_vector_mag(geo_vector_sub(vecA, vecB)));
  }
  case ScriptType_Count:
    break;
  }
  diag_assert_fail("Invalid script value");
  UNREACHABLE
}

ScriptVal script_val_norm(const ScriptVal val) {
  return script_type(val) == ScriptType_Vector3
             ? script_vector3(geo_vector_norm(val_as_vector3(val)))
             : script_null();
}

ScriptVal script_val_mag(const ScriptVal val) {
  switch (script_type(val)) {
  case ScriptType_Null:
  case ScriptType_Bool:
  case ScriptType_Entity:
  case ScriptType_String:
    return script_null();
  case ScriptType_Number:
    return script_number(math_abs(val_as_number(val)));
  case ScriptType_Vector3:
    return script_number(geo_vector_mag(val_as_vector3(val)));
  case ScriptType_Count:
    break;
  }
  diag_assert_fail("Invalid script value");
  UNREACHABLE
}

ScriptVal script_val_angle(const ScriptVal a, const ScriptVal b) {
  return (script_type(a) == ScriptType_Vector3 && script_type(b) == ScriptType_Vector3)
             ? script_number(geo_vector_angle(val_as_vector3(a), val_as_vector3(b)))
             : script_null();
}

ScriptVal script_val_random() { return script_number(rng_sample_f32(g_rng)); }

ScriptVal script_val_random_between(const ScriptVal a, const ScriptVal b) {
  if (script_type(a) != script_type(b)) {
    return script_null();
  }
  switch (script_type(a)) {
  case ScriptType_Null:
  case ScriptType_Bool:
  case ScriptType_Entity:
  case ScriptType_String:
    return script_null();
  case ScriptType_Number:
    return script_number(rng_sample_range(g_rng, val_as_number(a), val_as_number(b)));
  case ScriptType_Vector3: {
    const GeoVector vecA = val_as_vector3_dirty_w(a);
    const GeoVector vecB = val_as_vector3_dirty_w(b);
    return script_vector3_lit(
        rng_sample_range(g_rng, vecA.x, vecB.x),
        rng_sample_range(g_rng, vecA.y, vecB.y),
        rng_sample_range(g_rng, vecA.z, vecB.z));
  }
  case ScriptType_Count:
    break;
  }
  diag_assert_fail("Invalid script value");
  UNREACHABLE
}

ScriptVal script_val_round_down(const ScriptVal val) {
  switch (script_type(val)) {
  case ScriptType_Null:
  case ScriptType_Bool:
  case ScriptType_Entity:
  case ScriptType_String:
    return script_null();
  case ScriptType_Number:
    return script_number(math_round_down_f64(val_as_number(val)));
  case ScriptType_Vector3:
    return script_vector3(geo_vector_round_down(val_as_vector3_dirty_w(val)));
  case ScriptType_Count:
    break;
  }
  diag_assert_fail("Invalid script value");
  UNREACHABLE
}

ScriptVal script_val_round_nearest(const ScriptVal val) {
  switch (script_type(val)) {
  case ScriptType_Null:
  case ScriptType_Bool:
  case ScriptType_Entity:
  case ScriptType_String:
    return script_null();
  case ScriptType_Number:
    return script_number(math_round_nearest_f64(val_as_number(val)));
  case ScriptType_Vector3:
    return script_vector3(geo_vector_round_nearest(val_as_vector3_dirty_w(val)));
  case ScriptType_Count:
    break;
  }
  diag_assert_fail("Invalid script value");
  UNREACHABLE
}

ScriptVal script_val_round_up(const ScriptVal val) {
  switch (script_type(val)) {
  case ScriptType_Null:
  case ScriptType_Bool:
  case ScriptType_Entity:
  case ScriptType_String:
    return script_null();
  case ScriptType_Number:
    return script_number(math_round_up_f64(val_as_number(val)));
  case ScriptType_Vector3:
    return script_vector3(geo_vector_round_up(val_as_vector3_dirty_w(val)));
  case ScriptType_Count:
    break;
  }
  diag_assert_fail("Invalid script value");
  UNREACHABLE
}

ScriptVal script_val_compose_vector3(const ScriptVal x, const ScriptVal y, const ScriptVal z) {
  if (script_type(x) != ScriptType_Number || script_type(y) != ScriptType_Number ||
      script_type(z) != ScriptType_Number) {
    return script_null();
  }
  return script_vector3_lit((f32)val_as_number(x), (f32)val_as_number(y), (f32)val_as_number(z));
}

ScriptVal script_val_vector_x(const ScriptVal val) {
  return script_type(val) == ScriptType_Vector3 ? script_number(val_as_vector3_dirty_w(val).x)
                                                : script_null();
}

ScriptVal script_val_vector_y(const ScriptVal val) {
  return script_type(val) == ScriptType_Vector3 ? script_number(val_as_vector3_dirty_w(val).y)
                                                : script_null();
}

ScriptVal script_val_vector_z(const ScriptVal val) {
  return script_type(val) == ScriptType_Vector3 ? script_number(val_as_vector3_dirty_w(val).z)
                                                : script_null();
}
