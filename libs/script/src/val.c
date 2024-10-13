#include "core_alloc.h"
#include "core_array.h"
#include "core_diag.h"
#include "core_float.h"
#include "core_intrinsic.h"
#include "core_math.h"
#include "core_noise.h"
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

ScriptVal script_null(void) { return val_null(); }
ScriptVal script_num(const f64 value) { return val_num(value); }
ScriptVal script_bool(const bool value) { return val_bool(value); }
ScriptVal script_vec3(const GeoVector value) { return val_vec3(value); }
ScriptVal script_vec3_lit(const f32 x, const f32 y, const f32 z) {
  return val_vec3(geo_vector(x, y, z));
}
ScriptVal script_quat(const GeoQuat q) { return val_quat(q); }
ScriptVal script_color(const GeoColor q) { return val_color(q); }
ScriptVal script_entity(const EcsEntityId entity) {
  diag_assert_msg(ecs_entity_valid(entity), "Invalid entity id; use script_entity_or_null()");
  return val_entity(entity);
}
ScriptVal script_entity_or_null(const EcsEntityId entity) {
  return ecs_entity_valid(entity) ? val_entity(entity) : val_null();
}
ScriptVal script_str(const StringHash str) { return val_str(str); }

ScriptVal script_time(const TimeDuration value) { return val_num(value / (f64)time_second); }

f64 script_get_num(const ScriptVal value, const f64 fallback) {
  return val_type(value) == ScriptType_Num ? val_as_num(value) : fallback;
}

bool script_get_bool(const ScriptVal value, const bool fallback) {
  return val_type(value) == ScriptType_Bool ? val_as_bool(value) : fallback;
}

GeoVector script_get_vec3(const ScriptVal value, const GeoVector fallback) {
  return val_type(value) == ScriptType_Vec3 ? val_as_vec3(value) : fallback;
}

GeoQuat script_get_quat(const ScriptVal value, const GeoQuat fallback) {
  return val_type(value) == ScriptType_Quat ? val_as_quat(value) : fallback;
}

GeoColor script_get_color(const ScriptVal value, const GeoColor fallback) {
  return val_type(value) == ScriptType_Color ? val_as_color(value) : fallback;
}

EcsEntityId script_get_entity(const ScriptVal value, const EcsEntityId fallback) {
  return val_type(value) == ScriptType_Entity ? val_as_entity(value) : fallback;
}

StringHash script_get_str(const ScriptVal value, const StringHash fallback) {
  return val_type(value) == ScriptType_Str ? val_as_str(value) : fallback;
}

TimeDuration script_get_time(const ScriptVal value, const TimeDuration fallback) {
  return val_type(value) == ScriptType_Num ? (TimeDuration)time_seconds(val_as_num(value))
                                           : fallback;
}

bool script_truthy(const ScriptVal value) {
  switch (val_type(value)) {
  case ScriptType_Null:
    return false;
  case ScriptType_Num:
    return val_as_num(value) != 0;
  case ScriptType_Bool:
    return val_as_bool(value);
  case ScriptType_Vec3:
  case ScriptType_Quat:
  case ScriptType_Color:
    /**
     * NOTE: At the moment vectors, quaternions and colors are always considered to be truthy. This
     * is arguably inconsistent with numbers where we treat 0 as falsy. However its unclear what
     * good truthy semantics are for these types, for example is a unit-quaternion truthy or not?
     */
    return true;
  case ScriptType_Entity:
    return true; // Only valid entities can be stored in values.
  case ScriptType_Str:
    return val_as_str(value) != 0;
  case ScriptType_Count:
    break;
  }
  diag_assert_fail("Invalid script value");
  UNREACHABLE
}

ScriptVal script_truthy_as_val(const ScriptVal value) { return val_bool(script_truthy(value)); }

bool script_falsy(const ScriptVal value) { return !script_truthy(value); }

ScriptVal script_falsy_as_val(const ScriptVal value) { return val_bool(!script_truthy(value)); }

bool script_non_null(const ScriptVal value) { return val_type(value) != ScriptType_Null; }

ScriptVal script_non_null_as_val(const ScriptVal value) {
  return val_bool(val_type(value) != ScriptType_Null);
}

ScriptVal script_val_or(const ScriptVal value, const ScriptVal fallback) {
  return val_type(value) ? value : fallback;
}

u32 script_hash(const ScriptVal value) {
  const u32 tHash = script_val_type_hash(val_type(value));
  switch (val_type(value)) {
  case ScriptType_Null:
    return tHash;
  case ScriptType_Num:
    return bits_hash_32_combine(tHash, bits_hash_32(mem_create(value.bytes, sizeof(f64))));
  case ScriptType_Bool:
    return bits_hash_32_combine(tHash, bits_hash_32(mem_create(value.bytes, sizeof(bool))));
  case ScriptType_Vec3:
    return bits_hash_32_combine(tHash, bits_hash_32(mem_create(value.bytes, sizeof(f32) * 3)));
  case ScriptType_Quat:
    return bits_hash_32_combine(tHash, bits_hash_32(mem_create(value.bytes, sizeof(f32) * 3)));
  case ScriptType_Color:
    return bits_hash_32_combine(tHash, bits_hash_32(mem_create(value.bytes, sizeof(f16) * 4)));
  case ScriptType_Entity:
    return bits_hash_32_combine(tHash, bits_hash_32(mem_create(value.bytes, sizeof(EcsEntityId))));
  case ScriptType_Str:
    return bits_hash_32_combine(tHash, val_as_str(value));
  case ScriptType_Count:
    break;
  }
  diag_assert_fail("Invalid script value");
  UNREACHABLE
}

String script_val_type_str(const ScriptType type) {
  diag_assert_msg(type < ScriptType_Count, "Invalid script value type: {}", fmt_int(type));
  static const String g_names[] = {
      string_static("null"),
      string_static("num"),
      string_static("bool"),
      string_static("vec3"),
      string_static("quat"),
      string_static("color"),
      string_static("entity"),
      string_static("str"),
  };
  ASSERT(array_elems(g_names) == ScriptType_Count, "Incorrect number of names");
  return g_names[type];
}

static StringHash g_valTypeHashes[ScriptType_Count];

static void val_type_hashes_init(void) {
  static bool           g_hashesInit;
  static ThreadSpinLock g_hashesInitLock;
  if (UNLIKELY(!g_hashesInit)) {
    thread_spinlock_lock(&g_hashesInitLock);
    if (!g_hashesInit) {
      for (ScriptType t = 0; t != ScriptType_Count; ++t) {
        g_valTypeHashes[t] = stringtable_add(g_stringtable, script_val_type_str(t));
      }
      g_hashesInit = true;
    }
    thread_spinlock_unlock(&g_hashesInitLock);
  }
}

StringHash script_val_type_hash(const ScriptType type) {
  diag_assert_msg(type < ScriptType_Count, "Invalid script value type: {}", fmt_int(type));
  val_type_hashes_init();
  return g_valTypeHashes[type];
}

ScriptType script_val_type_from_hash(const StringHash hash) {
  val_type_hashes_init();
  for (ScriptType t = 0; t != ScriptType_Count; ++t) {
    if (hash == g_valTypeHashes[t]) {
      return t;
    }
  }
  return ScriptType_Null; // TODO: Should we return a sentinel instead?
}

void script_val_write(const ScriptVal value, DynString* str) {
  switch (val_type(value)) {
  case ScriptType_Null:
    dynstring_append(str, string_lit("null"));
    return;
  case ScriptType_Num:
    format_write_f64(str, val_as_num(value), &format_opts_float(.expThresholdPos = 1e10));
    return;
  case ScriptType_Bool:
    format_write_bool(str, val_as_bool(value));
    return;
  case ScriptType_Vec3: {
    const GeoVector v = val_as_vec3_dirty_w(value);
    format_write_arg(str, &fmt_list_lit(fmt_float(v.x), fmt_float(v.y), fmt_float(v.z)));
    return;
  }
  case ScriptType_Quat: {
    const GeoQuat q = val_as_quat(value);
    format_write_arg(
        str, &fmt_list_lit(fmt_float(q.x), fmt_float(q.y), fmt_float(q.z), fmt_float(q.w)));
    return;
  }
  case ScriptType_Color: {
    const GeoColor c = val_as_color(value);
    format_write_arg(
        str, &fmt_list_lit(fmt_float(c.r), fmt_float(c.g), fmt_float(c.b), fmt_float(c.a)));
    return;
  }
  case ScriptType_Entity:
    format_write_u64(str, val_as_entity(value), &format_opts_int(.base = 16, .minDigits = 16));
    return;
  case ScriptType_Str: {
    const String valueString = stringtable_lookup(g_stringtable, val_as_str(value));
    if (string_is_empty(valueString)) {
      fmt_write(str, "#{}", string_hash_fmt(val_as_str(value)));
    } else {
      dynstring_append(str, valueString);
    }
    return;
  }
  case ScriptType_Count:
    break;
  }
  diag_assert_fail("Invalid script value");
  UNREACHABLE
}

String script_val_scratch(const ScriptVal value) {
  const Mem scratchMem = alloc_alloc(g_allocScratch, 128, 1);
  DynString str        = dynstring_create_over(scratchMem);

  script_val_write(value, &str);

  const String res = dynstring_view(&str);
  dynstring_destroy(&str);
  return res;
}

void script_mask_write(ScriptMask mask, DynString* str) {
  if (mask == script_mask_any) {
    dynstring_append(str, string_lit("any"));
    return;
  }
  if (mask == script_mask_none) {
    dynstring_append(str, string_lit("none"));
    return;
  }
  if ((mask & (1 << ScriptType_Null)) && intrinsic_popcnt_32(mask) == 2) {
    mask ^= 1 << ScriptType_Null; // Clear the null bit.
    const ScriptType type = (ScriptType)bitset_next(bitset_from_var(mask), 0);
    dynstring_append(str, script_val_type_str(type));
    dynstring_append_char(str, '?');
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

String script_mask_scratch(const ScriptMask mask) {
  const Mem scratchMem = alloc_alloc(g_allocScratch, 256, 1);
  DynString str        = dynstring_create_over(scratchMem);

  script_mask_write(mask, &str);

  const String res = dynstring_view(&str);
  dynstring_destroy(&str);
  return res;
}

bool script_val_equal(const ScriptVal a, const ScriptVal b) {
  if (val_type(a) != val_type(b)) {
    return false;
  }
  switch (val_type(a)) {
  case ScriptType_Null:
    return true;
  case ScriptType_Num:
    return math_abs(val_as_num(a) - val_as_num(b)) < 1e-6f;
  case ScriptType_Bool:
    return val_as_bool(a) == val_as_bool(b);
  case ScriptType_Vec3: {
    const GeoVector vecA = val_as_vec3_dirty_w(a);
    const GeoVector vecB = val_as_vec3_dirty_w(b);
    return geo_vector_equal3(vecA, vecB, 1e-6f);
  }
  case ScriptType_Quat: {
    const GeoQuat qA = val_as_quat(a);
    const GeoQuat qB = val_as_quat(b);
    return math_abs(geo_quat_dot(qA, qB)) > 1.0f - 1e-4f;
  }
  case ScriptType_Color: {
    const GeoColor colA = val_as_color(a);
    const GeoColor colB = val_as_color(b);
    return geo_color_equal(colA, colB, 1e-4f);
  }
  case ScriptType_Entity:
    return val_as_entity(a) == val_as_entity(b);
  case ScriptType_Str:
    return val_as_str(a) == val_as_str(b);
  case ScriptType_Count:
    break;
  }
  diag_assert_fail("Invalid script value");
  UNREACHABLE
}

ScriptVal script_val_equal_as_val(const ScriptVal a, const ScriptVal b) {
  return val_bool(script_val_equal(a, b));
}

bool script_val_less(const ScriptVal a, const ScriptVal b) {
  if (val_type(a) != val_type(b)) {
    return false;
  }
  switch (val_type(a)) {
  case ScriptType_Null:
  case ScriptType_Str:
  case ScriptType_Quat:
    return false;
  case ScriptType_Num:
    return val_as_num(a) < val_as_num(b);
  case ScriptType_Bool:
    return val_as_bool(a) < val_as_bool(b); // NOTE: Questionable usefulness?
  case ScriptType_Vec3:
    return geo_vector_mag(val_as_vec3(a)) < geo_vector_mag(val_as_vec3(b));
  case ScriptType_Color:
    return geo_color_mag(val_as_color(a)) < geo_color_mag(val_as_color(b));
  case ScriptType_Entity:
    return ecs_entity_id_serial(val_as_entity(a)) < ecs_entity_id_serial(val_as_entity(b));
  case ScriptType_Count:
    break;
  }
  diag_assert_fail("Invalid script value");
  UNREACHABLE
}

ScriptVal script_val_less_as_val(const ScriptVal a, const ScriptVal b) {
  return val_bool(script_val_less(a, b));
}

bool script_val_greater(const ScriptVal a, const ScriptVal b) {
  if (val_type(a) != val_type(b)) {
    return false;
  }
  switch (val_type(a)) {
  case ScriptType_Null:
  case ScriptType_Str:
  case ScriptType_Quat:
    return false;
  case ScriptType_Num:
    return val_as_num(a) > val_as_num(b);
  case ScriptType_Bool:
    return val_as_bool(a) > val_as_bool(b);
  case ScriptType_Vec3:
    return geo_vector_mag(val_as_vec3(a)) > geo_vector_mag(val_as_vec3(b));
  case ScriptType_Color:
    return geo_color_mag(val_as_color(a)) > geo_color_mag(val_as_color(b));
  case ScriptType_Entity:
    return ecs_entity_id_serial(val_as_entity(a)) > ecs_entity_id_serial(val_as_entity(b));
  case ScriptType_Count:
    break;
  }
  diag_assert_fail("Invalid script value");
  UNREACHABLE
}

ScriptVal script_val_greater_as_val(const ScriptVal a, const ScriptVal b) {
  return val_bool(script_val_greater(a, b));
}

ScriptVal script_val_type(const ScriptVal val) {
  val_type_hashes_init();
  return val_str(g_valTypeHashes[val_type(val)]);
}

ScriptVal script_val_hash(const ScriptVal val) { return val_num(script_hash(val)); }

ScriptVal script_val_neg(const ScriptVal val) {
  switch (val_type(val)) {
  case ScriptType_Null:
  case ScriptType_Bool:
  case ScriptType_Entity:
  case ScriptType_Str:
    return val_null();
  case ScriptType_Num:
    return val_num(-val_as_num(val));
  case ScriptType_Vec3: {
    const GeoVector vec = val_as_vec3_dirty_w(val);
    return val_vec3(geo_vector_mul(vec, -1.0f));
  }
  case ScriptType_Quat: {
    const GeoQuat q = val_as_quat(val);
    return val_quat(geo_quat_inverse(q));
  }
  case ScriptType_Color: {
    const GeoColor c = val_as_color(val);
    return val_color(geo_color_mul(c, -1.0f));
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
  case ScriptType_Str:
  case ScriptType_Quat:
    return val_null();
  case ScriptType_Num:
    return val_num(val_as_num(a) + val_as_num(b));
  case ScriptType_Vec3: {
    const GeoVector vecA = val_as_vec3_dirty_w(a);
    const GeoVector vecB = val_as_vec3_dirty_w(b);
    return val_vec3(geo_vector_add(vecA, vecB));
  }
  case ScriptType_Color: {
    const GeoColor colA = val_as_color(a);
    const GeoColor colB = val_as_color(b);
    return val_color(geo_color_add(colA, colB));
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
  case ScriptType_Str:
  case ScriptType_Quat:
    return val_null();
  case ScriptType_Num:
    return val_num(val_as_num(a) - val_as_num(b));
  case ScriptType_Vec3: {
    const GeoVector vecA = val_as_vec3_dirty_w(a);
    const GeoVector vecB = val_as_vec3_dirty_w(b);
    return val_vec3(geo_vector_sub(vecA, vecB));
  }
  case ScriptType_Color: {
    const GeoColor colA = val_as_color(a);
    const GeoColor colB = val_as_color(b);
    return val_color(geo_color_sub(colA, colB));
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
  case ScriptType_Str:
    return val_null();
  case ScriptType_Num:
    return val_type(b) == ScriptType_Num ? val_num(val_as_num(a) * val_as_num(b)) : val_null();
  case ScriptType_Vec3: {
    if (val_type(b) == ScriptType_Num) {
      const GeoVector vecA = val_as_vec3_dirty_w(a);
      return val_vec3(geo_vector_mul(vecA, (f32)val_as_num(b)));
    }
    if (val_type(b) == ScriptType_Vec3) {
      const GeoVector vecA = val_as_vec3_dirty_w(a);
      const GeoVector vecB = val_as_vec3_dirty_w(b);
      return val_vec3(geo_vector_mul_comps(vecA, vecB));
    }
    return val_null();
  }
  case ScriptType_Quat:
    if (val_type(b) == ScriptType_Vec3) {
      const GeoQuat   qA   = val_as_quat(a);
      const GeoVector vecB = val_as_vec3_dirty_w(b);
      return val_vec3(geo_quat_rotate(qA, vecB));
    }
    if (val_type(b) == ScriptType_Quat) {
      const GeoQuat qA = val_as_quat(a);
      const GeoQuat qB = val_as_quat(b);
      return val_quat(geo_quat_mul(qA, qB));
    }
    return val_null();
  case ScriptType_Color: {
    if (val_type(b) == ScriptType_Num) {
      const GeoColor c = val_as_color(a);
      return val_color(geo_color_mul(c, (f32)val_as_num(b)));
    }
    if (val_type(b) == ScriptType_Color) {
      const GeoColor colA = val_as_color(a);
      const GeoColor colB = val_as_color(b);
      return val_color(geo_color_mul_comps(colA, colB));
    }
    return val_null();
  }
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
  case ScriptType_Str:
  case ScriptType_Quat:
    return val_null();
  case ScriptType_Num:
    return val_type(b) == ScriptType_Num ? val_num(val_as_num(a) / val_as_num(b)) : val_null();
  case ScriptType_Vec3: {
    if (val_type(b) == ScriptType_Num) {
      const GeoVector vecA = val_as_vec3_dirty_w(a);
      return val_vec3(geo_vector_div(vecA, (f32)val_as_num(b)));
    }
    if (val_type(b) == ScriptType_Vec3) {
      const GeoVector vecA = val_as_vec3_dirty_w(a);
      const GeoVector vecB = val_as_vec3_dirty_w(b);
      return val_vec3(geo_vector_div_comps(vecA, vecB));
    }
    return val_null();
  }
  case ScriptType_Color: {
    if (val_type(b) == ScriptType_Num) {
      const GeoColor c = val_as_color(a);
      return val_color(geo_color_div(c, (f32)val_as_num(b)));
    }
    if (val_type(b) == ScriptType_Color) {
      const GeoColor colA = val_as_color(a);
      const GeoColor colB = val_as_color(b);
      return val_color(geo_color_div_comps(colA, colB));
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
  case ScriptType_Str:
  case ScriptType_Quat:
  case ScriptType_Color:
    return val_null();
  case ScriptType_Num:
    return val_type(b) == ScriptType_Num ? val_num(intrinsic_fmod_f64(val_as_num(a), val_as_num(b)))
                                         : val_null();
  case ScriptType_Vec3: {
    if (val_type(b) == ScriptType_Num) {
      const GeoVector vecA    = val_as_vec3_dirty_w(a);
      const f32       scalarB = (f32)val_as_num(b);
      return val_vec3(geo_vector(
          intrinsic_fmod_f32(vecA.x, scalarB),
          intrinsic_fmod_f32(vecA.y, scalarB),
          intrinsic_fmod_f32(vecA.z, scalarB)));
    }
    if (val_type(b) == ScriptType_Vec3) {
      const GeoVector vecA = val_as_vec3_dirty_w(a);
      const GeoVector vecB = val_as_vec3_dirty_w(b);
      return val_vec3(geo_vector(
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
  case ScriptType_Str:
  case ScriptType_Quat:
    return val_null();
  case ScriptType_Num:
    return val_num(math_abs(val_as_num(a) - val_as_num(b)));
  case ScriptType_Vec3: {
    const GeoVector vecA = val_as_vec3_dirty_w(a);
    const GeoVector vecB = val_as_vec3_dirty_w(b);
    return val_num(geo_vector_mag(geo_vector_sub(vecA, vecB)));
  }
  case ScriptType_Color: {
    const GeoColor colA = val_as_color(a);
    const GeoColor colB = val_as_color(b);
    return val_num(geo_color_mag(geo_color_sub(colA, colB)));
  }
  case ScriptType_Count:
    break;
  }
  diag_assert_fail("Invalid script value");
  UNREACHABLE
}

ScriptVal script_val_norm(const ScriptVal val) {
  switch (val_type(val)) {
  case ScriptType_Vec3:
    return val_vec3(geo_vector_norm(val_as_vec3(val)));
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
  case ScriptType_Str:
  case ScriptType_Quat:
    return val_null();
  case ScriptType_Num:
    return val_num(math_abs(val_as_num(val)));
  case ScriptType_Vec3:
    return val_num(geo_vector_mag(val_as_vec3(val)));
  case ScriptType_Color:
    return val_num(geo_color_mag(val_as_color(val)));
  case ScriptType_Count:
    break;
  }
  diag_assert_fail("Invalid script value");
  UNREACHABLE
}

ScriptVal script_val_abs(const ScriptVal val) {
  switch (val_type(val)) {
  case ScriptType_Null:
  case ScriptType_Bool:
  case ScriptType_Entity:
  case ScriptType_Str:
  case ScriptType_Quat:
    return val_null();
  case ScriptType_Num:
    return val_num(math_abs(val_as_num(val)));
  case ScriptType_Vec3:
    return val_vec3(geo_vector_abs(val_as_vec3(val)));
  case ScriptType_Color:
    return val_color(geo_color_abs(val_as_color(val)));
  case ScriptType_Count:
    break;
  }
  diag_assert_fail("Invalid script value");
  UNREACHABLE
}

ScriptVal script_val_angle(const ScriptVal a, const ScriptVal b) {
  if (val_type(a) == ScriptType_Vec3 && val_type(b) == ScriptType_Vec3) {
    return val_num(geo_vector_angle(val_as_vec3(a), val_as_vec3(b)));
  }
  if (val_type(a) == ScriptType_Quat && val_type(b) == ScriptType_Quat) {
    const GeoQuat qA    = val_as_quat(a);
    const GeoQuat qB    = val_as_quat(b);
    const GeoQuat delta = geo_quat_from_to(qA, qB);
    const f32     angle = geo_quat_to_angle(delta);
    return val_num(angle);
  }
  return val_null();
}

ScriptVal script_val_sin(const ScriptVal val) {
  if (val_type(val) == ScriptType_Num) {
    return val_num(intrinsic_sin_f64(val_as_num(val)));
  }
  return val_null();
}

ScriptVal script_val_cos(const ScriptVal val) {
  if (val_type(val) == ScriptType_Num) {
    return val_num(intrinsic_cos_f64(val_as_num(val)));
  }
  return val_null();
}

ScriptVal script_val_random(void) { return val_num(rng_sample_f32(g_rng)); }

ScriptVal script_val_random_sphere(void) { return val_vec3(geo_vector_rand_in_sphere3(g_rng)); }

ScriptVal script_val_random_circle_xz(void) {
  const f32 r     = math_sqrt_f32(rng_sample_f32(g_rng));
  const f32 theta = rng_sample_f32(g_rng) * 2.0f * math_pi_f32;
  return val_vec3(geo_vector(r * math_cos_f32(theta), 0, r * math_sin_f32(theta)));
}

ScriptVal script_val_random_between(const ScriptVal a, const ScriptVal b) {
  if (val_type(a) != val_type(b)) {
    return val_null();
  }
  switch (val_type(a)) {
  case ScriptType_Null:
  case ScriptType_Bool:
  case ScriptType_Entity:
  case ScriptType_Str:
  case ScriptType_Quat:
    return val_null();
  case ScriptType_Num:
    return val_num(rng_sample_range(g_rng, val_as_num(a), val_as_num(b)));
  case ScriptType_Vec3: {
    const GeoVector vecA = val_as_vec3_dirty_w(a);
    const GeoVector vecB = val_as_vec3_dirty_w(b);
    return val_vec3(geo_vector(
        rng_sample_range(g_rng, vecA.x, vecB.x),
        rng_sample_range(g_rng, vecA.y, vecB.y),
        rng_sample_range(g_rng, vecA.z, vecB.z)));
  }
  case ScriptType_Color: {
    const GeoColor colA = val_as_color(a);
    const GeoColor colB = val_as_color(b);
    return val_color(geo_color_lerp(colA, colB, rng_sample_f32(g_rng)));
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
  case ScriptType_Str:
  case ScriptType_Quat:
  case ScriptType_Color:
    return val_null();
  case ScriptType_Num:
    return val_num(math_round_down_f64(val_as_num(val)));
  case ScriptType_Vec3:
    return val_vec3(geo_vector_round_down(val_as_vec3_dirty_w(val)));
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
  case ScriptType_Str:
  case ScriptType_Quat:
  case ScriptType_Color:
    return val_null();
  case ScriptType_Num:
    return val_num(math_round_nearest_f64(val_as_num(val)));
  case ScriptType_Vec3:
    return val_vec3(geo_vector_round_nearest(val_as_vec3_dirty_w(val)));
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
  case ScriptType_Str:
  case ScriptType_Quat:
  case ScriptType_Color:
    return val_null();
  case ScriptType_Num:
    return val_num(math_round_up_f64(val_as_num(val)));
  case ScriptType_Vec3:
    return val_vec3(geo_vector_round_up(val_as_vec3_dirty_w(val)));
  case ScriptType_Count:
    break;
  }
  diag_assert_fail("Invalid script value");
  UNREACHABLE
}

ScriptVal script_val_clamp(const ScriptVal v, const ScriptVal min, const ScriptVal max) {
  switch (val_type(v)) {
  case ScriptType_Null:
  case ScriptType_Bool:
  case ScriptType_Entity:
  case ScriptType_Str:
    return val_null();
  case ScriptType_Vec3: {
    if (val_type(max) == ScriptType_Num) {
      // TODO: 'min' value is not used in vector clamping with a scalar.
      const f32 maxV = (f32)val_as_num(max);
      if (UNLIKELY(maxV <= 0.0f)) {
        return val_null();
      }
      return val_vec3(geo_vector_clamp(val_as_vec3(v), maxV));
    }
    if (val_type(min) == ScriptType_Vec3 && val_type(max) == ScriptType_Vec3) {
      return val_vec3(geo_vector_clamp_comps(val_as_vec3(v), val_as_vec3(min), val_as_vec3(max)));
    }
    return val_null();
  }
  case ScriptType_Quat: {
    if (val_type(max) == ScriptType_Num) {
      // TODO: 'min' value is not used in quaternion clamping with a scalar.
      const f32 maxV = (f32)val_as_num(max);
      if (UNLIKELY(maxV <= 0.0f)) {
        return val_null();
      }
      GeoQuat q = val_as_quat(v);
      return geo_quat_clamp(&q, maxV) ? val_quat(q) : v;
    }
    return val_null();
  }
  case ScriptType_Color: {
    if (val_type(max) == ScriptType_Num) {
      // TODO: 'min' value is not used in color clamping with a scalar.
      const f32 maxV = (f32)val_as_num(max);
      if (UNLIKELY(maxV <= 0.0f)) {
        return val_null();
      }
      return val_color(geo_color_clamp(val_as_color(v), maxV));
    }
    if (val_type(min) == ScriptType_Color && val_type(max) == ScriptType_Color) {
      return val_color(
          geo_color_clamp_comps(val_as_color(v), val_as_color(min), val_as_color(max)));
    }
    return val_null();
  }
  case ScriptType_Num: {
    if (val_type(min) == ScriptType_Num && val_type(max) == ScriptType_Num) {
      return val_num(math_clamp_f64(val_as_num(v), val_as_num(min), val_as_num(max)));
    }
    return val_null();
  }
  case ScriptType_Count:
    break;
  }
  diag_assert_fail("Invalid script value");
  UNREACHABLE
}

ScriptVal script_val_lerp(const ScriptVal x, const ScriptVal y, const ScriptVal t) {
  if (val_type(t) != ScriptType_Num) {
    return val_null();
  }
  const f32 tFrac = (f32)val_as_num(t);
  if (val_type(x) != val_type(y)) {
    return val_null();
  }
  switch (val_type(x)) {
  case ScriptType_Null:
  case ScriptType_Bool:
  case ScriptType_Entity:
  case ScriptType_Str:
    return val_null();
  case ScriptType_Num:
    return val_num(math_lerp(val_as_num(x), val_as_num(y), tFrac));
  case ScriptType_Vec3:
    return val_vec3(geo_vector_lerp(val_as_vec3(x), val_as_vec3(y), tFrac));
  case ScriptType_Quat:
    return val_quat(geo_quat_slerp(val_as_quat(x), val_as_quat(y), tFrac));
  case ScriptType_Color:
    return val_color(geo_color_lerp(val_as_color(x), val_as_color(y), tFrac));
  case ScriptType_Count:
    break;
  }
  diag_assert_fail("Invalid script value");
  UNREACHABLE
}

ScriptVal script_val_min(const ScriptVal x, const ScriptVal y) {
  if (val_type(x) != val_type(y)) {
    return val_null();
  }
  switch (val_type(x)) {
  case ScriptType_Null:
  case ScriptType_Bool:
  case ScriptType_Entity:
  case ScriptType_Str:
  case ScriptType_Quat:
    return val_null();
  case ScriptType_Num:
    return val_num(math_min(val_as_num(x), val_as_num(y)));
  case ScriptType_Vec3:
    return val_vec3(geo_vector_min(val_as_vec3(x), val_as_vec3(y)));
  case ScriptType_Color:
    return val_color(geo_color_min(val_as_color(x), val_as_color(y)));
  case ScriptType_Count:
    break;
  }
  diag_assert_fail("Invalid script value");
  UNREACHABLE
}

ScriptVal script_val_max(const ScriptVal x, const ScriptVal y) {
  if (val_type(x) != val_type(y)) {
    return val_null();
  }
  switch (val_type(x)) {
  case ScriptType_Null:
  case ScriptType_Bool:
  case ScriptType_Entity:
  case ScriptType_Str:
  case ScriptType_Quat:
    return val_null();
  case ScriptType_Num:
    return val_num(math_max(val_as_num(x), val_as_num(y)));
  case ScriptType_Vec3:
    return val_vec3(geo_vector_max(val_as_vec3(x), val_as_vec3(y)));
  case ScriptType_Color:
    return val_color(geo_color_max(val_as_color(x), val_as_color(y)));
  case ScriptType_Count:
    break;
  }
  diag_assert_fail("Invalid script value");
  UNREACHABLE
}

ScriptVal script_val_perlin3(const ScriptVal val) {
  if (val_type(val) != ScriptType_Vec3) {
    return val_null();
  }
  const GeoVector pos = val_as_vec3(val);
  return val_num(noise_perlin3(pos.x, pos.y, pos.z));
}

ScriptVal script_val_vec3_compose(const ScriptVal x, const ScriptVal y, const ScriptVal z) {
  const ScriptType nT = ScriptType_Num;
  if (val_type(x) != nT || val_type(y) != nT || val_type(z) != nT) {
    return val_null();
  }
  return val_vec3(geo_vector((f32)val_as_num(x), (f32)val_as_num(y), (f32)val_as_num(z)));
}

ScriptVal script_val_vec_x(const ScriptVal val) {
  return val_type(val) == ScriptType_Vec3 ? val_num(val_as_vec3_dirty_w(val).x) : val_null();
}

ScriptVal script_val_vec_y(const ScriptVal val) {
  return val_type(val) == ScriptType_Vec3 ? val_num(val_as_vec3_dirty_w(val).y) : val_null();
}

ScriptVal script_val_vec_z(const ScriptVal val) {
  return val_type(val) == ScriptType_Vec3 ? val_num(val_as_vec3_dirty_w(val).z) : val_null();
}

ScriptVal script_val_quat_from_euler(const ScriptVal x, const ScriptVal y, const ScriptVal z) {
  const ScriptType numType = ScriptType_Num;
  if (val_type(x) != numType || val_type(y) != numType || val_type(z) != numType) {
    return val_null();
  }
  const GeoVector eulerAngles =
      geo_vector((f32)val_as_num(x), (f32)val_as_num(y), (f32)val_as_num(z));
  return val_quat(geo_quat_from_euler(eulerAngles));
}

ScriptVal script_val_quat_from_angle_axis(const ScriptVal angle, const ScriptVal axis) {
  if (val_type(angle) != ScriptType_Num || val_type(axis) != ScriptType_Vec3) {
    return val_null();
  }
  const GeoVector axisVec = val_as_vec3(axis);
  const f32       axisMag = geo_vector_mag(axisVec);
  if (axisMag <= f32_epsilon) {
    return val_null();
  }
  const GeoVector axisNorm = geo_vector_div(axisVec, axisMag);
  return val_quat(geo_quat_angle_axis((f32)val_as_num(angle), axisNorm));
}

ScriptVal script_val_color_compose(
    const ScriptVal r, const ScriptVal g, const ScriptVal b, const ScriptVal a) {
  const ScriptType nT = ScriptType_Num;
  if (val_type(r) != nT || val_type(g) != nT || val_type(b) != nT || val_type(a) != nT) {
    return val_null();
  }
  return val_color(
      geo_color((f32)val_as_num(r), (f32)val_as_num(g), (f32)val_as_num(b), (f32)val_as_num(a)));
}

ScriptVal script_val_color_compose_hsv(
    const ScriptVal h, const ScriptVal s, const ScriptVal v, const ScriptVal a) {
  const ScriptType nT = ScriptType_Num;
  if (val_type(h) != nT || val_type(s) != nT || val_type(v) != nT || val_type(a) != nT) {
    return val_null();
  }
  const f32 hue        = math_mod_f32(math_abs((f32)val_as_num(h)), 1.0f);
  const f32 saturation = math_clamp_f32((f32)val_as_num(s), 0.0f, 1.0f);
  const f32 value      = (f32)val_as_num(v);
  const f32 alpha      = (f32)val_as_num(a);

  return val_color(geo_color_from_hsv(hue, saturation, value, alpha));
}

ScriptVal script_val_color_for_val(const ScriptVal v) {
  const u32 hash = script_hash(v);
  return val_color(geo_color_for_hash(hash));
}
