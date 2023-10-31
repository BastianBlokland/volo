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
#include "ecs_entity.h"
#include "geo_quat.h"
#include "script_val.h"

#include "val_internal.h"

ScriptType script_type(const ScriptVal value) { return val_type(value); }

bool script_type_check(const ScriptVal value, const ScriptMask mask) {
  return val_type_check(value, mask);
}

ScriptVal script_null() { return val_null(); }
ScriptVal script_number(const f64 value) { return val_number(value); }
ScriptVal script_bool(const bool value) { return val_bool(value); }
ScriptVal script_vector3(const GeoVector value) { return val_vector3(value); }
ScriptVal script_vector3_lit(const f32 x, const f32 y, const f32 z) {
  return val_vector3(geo_vector(x, y, z));
}
ScriptVal script_quat(const GeoQuat q) { return val_quat(q); }
ScriptVal script_entity(const EcsEntityId value) { return val_entity(value); }
ScriptVal script_entity_or_null(const EcsEntityId entity) {
  return entity ? val_entity(entity) : val_null();
}
ScriptVal script_string(const StringHash str) { return val_string(str); }

ScriptVal script_time(const TimeDuration value) { return val_number(value / (f64)time_second); }

f64 script_get_number(const ScriptVal value, const f64 fallback) {
  return val_type(value) == ScriptType_Number ? val_as_number(value) : fallback;
}

bool script_get_bool(const ScriptVal value, const bool fallback) {
  return val_type(value) == ScriptType_Bool ? val_as_bool(value) : fallback;
}

GeoVector script_get_vector3(const ScriptVal value, const GeoVector fallback) {
  return val_type(value) == ScriptType_Vector3 ? val_as_vector3(value) : fallback;
}

GeoQuat script_get_quat(const ScriptVal value, const GeoQuat fallback) {
  return val_type(value) == ScriptType_Quat ? val_as_quat(value) : fallback;
}

EcsEntityId script_get_entity(const ScriptVal value, const EcsEntityId fallback) {
  return val_type(value) == ScriptType_Entity ? val_as_entity(value) : fallback;
}

StringHash script_get_string(const ScriptVal value, const StringHash fallback) {
  return val_type(value) == ScriptType_String ? val_as_string(value) : fallback;
}

TimeDuration script_get_time(const ScriptVal value, const TimeDuration fallback) {
  return val_type(value) == ScriptType_Number ? (TimeDuration)time_seconds(val_as_number(value))
                                              : fallback;
}

bool script_truthy(const ScriptVal value) {
  switch (val_type(value)) {
  case ScriptType_Null:
    return false;
  case ScriptType_Number:
    return val_as_number(value) != 0;
  case ScriptType_Bool:
    return val_as_bool(value);
  case ScriptType_Vector3:
    return geo_vector_mag_sqr(val_as_vector3(value)) > f32_epsilon;
  case ScriptType_Quat:
    return true; // Only unit quaternions are supported thus they are always truthy.
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

bool script_val_has(const ScriptVal value) { return val_type(value) != ScriptType_Null; }

ScriptVal script_val_or(const ScriptVal value, const ScriptVal fallback) {
  return val_type(value) ? value : fallback;
}

String script_val_type_str(const ScriptType type) {
  diag_assert_msg(type < ScriptType_Count, "Invalid script value type: {}", fmt_int(type));
  static const String g_names[] = {
      string_static("null"),
      string_static("number"),
      string_static("bool"),
      string_static("vector3"),
      string_static("quat"),
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
  switch (val_type(value)) {
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
  case ScriptType_Quat: {
    const GeoQuat q = val_as_quat(value);
    format_write_arg(
        str, &fmt_list_lit(fmt_float(q.x), fmt_float(q.y), fmt_float(q.z), fmt_float(q.w)));
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

void script_mask_str_write(const ScriptMask mask, DynString* str) {
  if (mask == script_mask_any) {
    dynstring_append(str, string_lit("any"));
    return;
  }
  if (mask == script_mask_none) {
    dynstring_append(str, string_lit("none"));
    return;
  }
  bool first = true;
  bitset_for(bitset_from_var(mask), typeIndex) {
    if (!first) {
      dynstring_append(str, string_lit(" | "));
    }
    first = false;
    dynstring_append(str, script_val_type_str((ScriptType)typeIndex));
  }
}

String script_mask_str_scratch(const ScriptMask mask) {
  const Mem scratchMem = alloc_alloc(g_alloc_scratch, 256, 1);
  DynString str        = dynstring_create_over(scratchMem);

  script_mask_str_write(mask, &str);

  const String res = dynstring_view(&str);
  dynstring_destroy(&str);
  return res;
}

bool script_val_equal(const ScriptVal a, const ScriptVal b) {
  if (val_type(a) != val_type(b)) {
    return false;
  }
  static const f32 g_scalarThreshold = 1e-6f;
  static const f32 g_vectorThreshold = 1e-6f;
  switch (val_type(a)) {
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
  case ScriptType_Quat: {
    const GeoQuat qA = val_as_quat(a);
    const GeoQuat qB = val_as_quat(b);
    return math_abs(geo_quat_dot(qA, qB)) > 1.0f - 1e-4f;
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
  if (val_type(a) != val_type(b)) {
    return false;
  }
  switch (val_type(a)) {
  case ScriptType_Null:
  case ScriptType_String:
  case ScriptType_Quat:
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
  if (val_type(a) != val_type(b)) {
    return false;
  }
  switch (val_type(a)) {
  case ScriptType_Null:
  case ScriptType_String:
  case ScriptType_Quat:
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
  switch (val_type(val)) {
  case ScriptType_Null:
  case ScriptType_Bool:
  case ScriptType_Entity:
  case ScriptType_String:
    return val_null();
  case ScriptType_Number:
    return val_number(-val_as_number(val));
  case ScriptType_Vector3: {
    const GeoVector vec = val_as_vector3_dirty_w(val);
    return val_vector3(geo_vector_mul(vec, -1.0f));
  }
  case ScriptType_Quat: {
    const GeoQuat q = val_as_quat(val);
    return val_quat(geo_quat_inverse(q));
  }
  case ScriptType_Count:
    break;
  }
  diag_assert_fail("Invalid script value");
  UNREACHABLE
}

ScriptVal script_val_inv(const ScriptVal val) { return val_bool(!script_truthy(val)); }

ScriptVal script_val_add(const ScriptVal a, const ScriptVal b) {
  if (val_type(a) != val_type(b)) {
    return val_null();
  }
  switch (val_type(a)) {
  case ScriptType_Null:
  case ScriptType_Bool:
  case ScriptType_Entity:
  case ScriptType_String:
  case ScriptType_Quat:
    return val_null();
  case ScriptType_Number:
    return val_number(val_as_number(a) + val_as_number(b));
  case ScriptType_Vector3: {
    const GeoVector vecA = val_as_vector3_dirty_w(a);
    const GeoVector vecB = val_as_vector3_dirty_w(b);
    return val_vector3(geo_vector_add(vecA, vecB));
  }
  case ScriptType_Count:
    break;
  }
  diag_assert_fail("Invalid script value");
  UNREACHABLE
}

ScriptVal script_val_sub(const ScriptVal a, const ScriptVal b) {
  if (val_type(a) != val_type(b)) {
    return val_null();
  }
  switch (val_type(a)) {
  case ScriptType_Null:
  case ScriptType_Bool:
  case ScriptType_Entity:
  case ScriptType_String:
  case ScriptType_Quat:
    return val_null();
  case ScriptType_Number:
    return val_number(val_as_number(a) - val_as_number(b));
  case ScriptType_Vector3: {
    const GeoVector vecA = val_as_vector3_dirty_w(a);
    const GeoVector vecB = val_as_vector3_dirty_w(b);
    return val_vector3(geo_vector_sub(vecA, vecB));
  }
  case ScriptType_Count:
    break;
  }
  diag_assert_fail("Invalid script value");
  UNREACHABLE
}

ScriptVal script_val_mul(const ScriptVal a, const ScriptVal b) {
  switch (val_type(a)) {
  case ScriptType_Null:
  case ScriptType_Bool:
  case ScriptType_Entity:
  case ScriptType_String:
    return val_null();
  case ScriptType_Number:
    return val_type(b) == ScriptType_Number ? val_number(val_as_number(a) * val_as_number(b))
                                            : val_null();
  case ScriptType_Vector3: {
    if (val_type(b) == ScriptType_Number) {
      const GeoVector vecA = val_as_vector3_dirty_w(a);
      return val_vector3(geo_vector_mul(vecA, (f32)val_as_number(b)));
    }
    if (val_type(b) == ScriptType_Vector3) {
      const GeoVector vecA = val_as_vector3_dirty_w(a);
      const GeoVector vecB = val_as_vector3_dirty_w(b);
      return val_vector3(geo_vector_mul_comps(vecA, vecB));
    }
    return val_null();
  }
  case ScriptType_Quat:
    if (val_type(b) == ScriptType_Vector3) {
      const GeoQuat   qA   = val_as_quat(a);
      const GeoVector vecB = val_as_vector3_dirty_w(b);
      return val_vector3(geo_quat_rotate(qA, vecB));
    }
    if (val_type(b) == ScriptType_Quat) {
      const GeoQuat qA = val_as_quat(a);
      const GeoQuat qB = val_as_quat(b);
      return val_quat(geo_quat_mul(qA, qB));
    }
    return val_null();
  case ScriptType_Count:
    break;
  }
  diag_assert_fail("Invalid script value");
  UNREACHABLE
}

ScriptVal script_val_div(const ScriptVal a, const ScriptVal b) {
  switch (val_type(a)) {
  case ScriptType_Null:
  case ScriptType_Bool:
  case ScriptType_Entity:
  case ScriptType_String:
  case ScriptType_Quat:
    return val_null();
  case ScriptType_Number:
    return val_type(b) == ScriptType_Number ? val_number(val_as_number(a) / val_as_number(b))
                                            : val_null();
  case ScriptType_Vector3: {
    if (val_type(b) == ScriptType_Number) {
      const GeoVector vecA = val_as_vector3_dirty_w(a);
      return val_vector3(geo_vector_div(vecA, (f32)val_as_number(b)));
    }
    if (val_type(b) == ScriptType_Vector3) {
      const GeoVector vecA = val_as_vector3_dirty_w(a);
      const GeoVector vecB = val_as_vector3_dirty_w(b);
      return val_vector3(geo_vector_div_comps(vecA, vecB));
    }
    return val_null();
  }
  case ScriptType_Count:
    break;
  }
  diag_assert_fail("Invalid script value");
  UNREACHABLE
}

ScriptVal script_val_mod(const ScriptVal a, const ScriptVal b) {
  switch (val_type(a)) {
  case ScriptType_Null:
  case ScriptType_Bool:
  case ScriptType_Entity:
  case ScriptType_String:
  case ScriptType_Quat:
    return val_null();
  case ScriptType_Number:
    return val_type(b) == ScriptType_Number
               ? val_number(intrinsic_fmod_f64(val_as_number(a), val_as_number(b)))
               : val_null();
  case ScriptType_Vector3: {
    if (val_type(b) == ScriptType_Number) {
      const GeoVector vecA    = val_as_vector3_dirty_w(a);
      const f32       scalarB = (f32)val_as_number(b);
      return val_vector3(geo_vector(
          intrinsic_fmod_f32(vecA.x, scalarB),
          intrinsic_fmod_f32(vecA.y, scalarB),
          intrinsic_fmod_f32(vecA.z, scalarB)));
    }
    if (val_type(b) == ScriptType_Vector3) {
      const GeoVector vecA = val_as_vector3_dirty_w(a);
      const GeoVector vecB = val_as_vector3_dirty_w(b);
      return val_vector3(geo_vector(
          intrinsic_fmod_f32(vecA.x, vecB.x),
          intrinsic_fmod_f32(vecA.y, vecB.y),
          intrinsic_fmod_f32(vecA.z, vecB.z)));
    }
    return val_null();
  }
  case ScriptType_Count:
    break;
  }
  diag_assert_fail("Invalid script value");
  UNREACHABLE
}

ScriptVal script_val_dist(const ScriptVal a, const ScriptVal b) {
  if (val_type(a) != val_type(b)) {
    return val_null();
  }
  switch (val_type(a)) {
  case ScriptType_Null:
  case ScriptType_Bool:
  case ScriptType_Entity:
  case ScriptType_String:
  case ScriptType_Quat:
    return val_null();
  case ScriptType_Number:
    return val_number(math_abs(val_as_number(a) - val_as_number(b)));
  case ScriptType_Vector3: {
    const GeoVector vecA = val_as_vector3_dirty_w(a);
    const GeoVector vecB = val_as_vector3_dirty_w(b);
    return val_number(geo_vector_mag(geo_vector_sub(vecA, vecB)));
  }
  case ScriptType_Count:
    break;
  }
  diag_assert_fail("Invalid script value");
  UNREACHABLE
}

ScriptVal script_val_norm(const ScriptVal val) {
  switch (val_type(val)) {
  case ScriptType_Vector3:
    return val_vector3(geo_vector_norm(val_as_vector3(val)));
  case ScriptType_Quat:
    return val; // NOTE: Quaternion script values are normalized on creation.
  default:
    return val_null();
  }
}

ScriptVal script_val_mag(const ScriptVal val) {
  switch (val_type(val)) {
  case ScriptType_Null:
  case ScriptType_Bool:
  case ScriptType_Entity:
  case ScriptType_String:
  case ScriptType_Quat:
    return val_null();
  case ScriptType_Number:
    return val_number(math_abs(val_as_number(val)));
  case ScriptType_Vector3:
    return val_number(geo_vector_mag(val_as_vector3(val)));
  case ScriptType_Count:
    break;
  }
  diag_assert_fail("Invalid script value");
  UNREACHABLE
}

ScriptVal script_val_angle(const ScriptVal a, const ScriptVal b) {
  if (val_type(a) == ScriptType_Vector3 && val_type(b) == ScriptType_Vector3) {
    return val_number(geo_vector_angle(val_as_vector3(a), val_as_vector3(b)));
  }
  if (val_type(a) == ScriptType_Quat && val_type(b) == ScriptType_Quat) {
    const GeoQuat qA    = val_as_quat(a);
    const GeoQuat qB    = val_as_quat(b);
    const GeoQuat delta = geo_quat_from_to(qA, qB);
    const f32     angle = geo_vector_mag(geo_quat_to_angle_axis(delta));
    return val_number(angle);
  }
  return val_null();
}

ScriptVal script_val_random() { return val_number(rng_sample_f32(g_rng)); }

ScriptVal script_val_random_sphere() { return val_vector3(geo_vector_rand_in_sphere3(g_rng)); }

ScriptVal script_val_random_circle_xz() {
  const f32 r     = math_sqrt_f32(rng_sample_f32(g_rng));
  const f32 theta = rng_sample_f32(g_rng) * 2.0f * math_pi_f32;
  return val_vector3(geo_vector(r * math_cos_f32(theta), 0, r * math_sin_f32(theta)));
}

ScriptVal script_val_random_between(const ScriptVal a, const ScriptVal b) {
  if (val_type(a) != val_type(b)) {
    return val_null();
  }
  switch (val_type(a)) {
  case ScriptType_Null:
  case ScriptType_Bool:
  case ScriptType_Entity:
  case ScriptType_String:
  case ScriptType_Quat:
    return val_null();
  case ScriptType_Number:
    return val_number(rng_sample_range(g_rng, val_as_number(a), val_as_number(b)));
  case ScriptType_Vector3: {
    const GeoVector vecA = val_as_vector3_dirty_w(a);
    const GeoVector vecB = val_as_vector3_dirty_w(b);
    return val_vector3(geo_vector(
        rng_sample_range(g_rng, vecA.x, vecB.x),
        rng_sample_range(g_rng, vecA.y, vecB.y),
        rng_sample_range(g_rng, vecA.z, vecB.z)));
  }
  case ScriptType_Count:
    break;
  }
  diag_assert_fail("Invalid script value");
  UNREACHABLE
}

ScriptVal script_val_round_down(const ScriptVal val) {
  switch (val_type(val)) {
  case ScriptType_Null:
  case ScriptType_Bool:
  case ScriptType_Entity:
  case ScriptType_String:
  case ScriptType_Quat:
    return val_null();
  case ScriptType_Number:
    return val_number(math_round_down_f64(val_as_number(val)));
  case ScriptType_Vector3:
    return val_vector3(geo_vector_round_down(val_as_vector3_dirty_w(val)));
  case ScriptType_Count:
    break;
  }
  diag_assert_fail("Invalid script value");
  UNREACHABLE
}

ScriptVal script_val_round_nearest(const ScriptVal val) {
  switch (val_type(val)) {
  case ScriptType_Null:
  case ScriptType_Bool:
  case ScriptType_Entity:
  case ScriptType_String:
  case ScriptType_Quat:
    return val_null();
  case ScriptType_Number:
    return val_number(math_round_nearest_f64(val_as_number(val)));
  case ScriptType_Vector3:
    return val_vector3(geo_vector_round_nearest(val_as_vector3_dirty_w(val)));
  case ScriptType_Count:
    break;
  }
  diag_assert_fail("Invalid script value");
  UNREACHABLE
}

ScriptVal script_val_round_up(const ScriptVal val) {
  switch (val_type(val)) {
  case ScriptType_Null:
  case ScriptType_Bool:
  case ScriptType_Entity:
  case ScriptType_String:
  case ScriptType_Quat:
    return val_null();
  case ScriptType_Number:
    return val_number(math_round_up_f64(val_as_number(val)));
  case ScriptType_Vector3:
    return val_vector3(geo_vector_round_up(val_as_vector3_dirty_w(val)));
  case ScriptType_Count:
    break;
  }
  diag_assert_fail("Invalid script value");
  UNREACHABLE
}

ScriptVal script_val_vector3_compose(const ScriptVal x, const ScriptVal y, const ScriptVal z) {
  const ScriptType numType = ScriptType_Number;
  if (val_type(x) != numType || val_type(y) != numType || val_type(z) != numType) {
    return val_null();
  }
  return val_vector3(
      geo_vector((f32)val_as_number(x), (f32)val_as_number(y), (f32)val_as_number(z)));
}

ScriptVal script_val_vector_x(const ScriptVal val) {
  return val_type(val) == ScriptType_Vector3 ? val_number(val_as_vector3_dirty_w(val).x)
                                             : val_null();
}

ScriptVal script_val_vector_y(const ScriptVal val) {
  return val_type(val) == ScriptType_Vector3 ? val_number(val_as_vector3_dirty_w(val).y)
                                             : val_null();
}

ScriptVal script_val_vector_z(const ScriptVal val) {
  return val_type(val) == ScriptType_Vector3 ? val_number(val_as_vector3_dirty_w(val).z)
                                             : val_null();
}

ScriptVal script_val_quat_from_euler(const ScriptVal x, const ScriptVal y, const ScriptVal z) {
  const ScriptType numType = ScriptType_Number;
  if (val_type(x) != numType || val_type(y) != numType || val_type(z) != numType) {
    return val_null();
  }
  const GeoVector eulerAngles =
      geo_vector((f32)val_as_number(x), (f32)val_as_number(y), (f32)val_as_number(z));
  return val_quat(geo_quat_from_euler(eulerAngles));
}

ScriptVal script_val_quat_from_angle_axis(const ScriptVal angle, const ScriptVal axis) {
  if (val_type(angle) != ScriptType_Number || val_type(axis) != ScriptType_Vector3) {
    return val_null();
  }
  return val_quat(geo_quat_angle_axis(val_as_vector3(axis), (f32)val_as_number(angle)));
}
