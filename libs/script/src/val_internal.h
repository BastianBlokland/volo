#pragma once
#include "core_float.h"
#include "core_intrinsic.h"
#include "geo_color.h"
#include "geo_quat.h"
#include "script_val.h"

/**
 * ScriptVal's are 128bit values with 128bit alignment.
 *
 * | Type    | Word 0        | Word 1        | Word 2     | Word 3       |
 * |---------|---------------|---------------|------------|--------------|
 * | null    | unused        | unused        | unused     | type tag (0) |
 * | num     | lower 32 bits | upper 32 bits | unused     | type tag (1) |
 * | bool    | 0 / 1         | unused        | unused     | type tag (2) |
 * | vec3    | f32 x         | f32 y         | f32 z      | type tag (3) |
 * | quat    | f32 q1        | f32 q2        | f32 q3     | type tag (4) |
 * | color   | r f16, g f16  | b f16, a f16  | unused     | type tag (5) |
 * | entity  | lower 32 bits | upper 32 bits | unused     | type tag (6) |
 * | str     | u32           | unused        | unused     | type tag (7) |
 *
 * NOTE: Only unit quaternions are supported (as the 4th component is reconstructed).
 * NOTE: Assumes little-endian byte order.
 */

/**
 * Index of the type byte inside a ScriptVal.
 * NOTE: Its debatable if we should store it in byte 12 or byte 15, reasoning for storing it in byte
 * 12 is then its the start of the 4th word and a 32 bit load can be used if needed.
 */
#define val_type_byte_index 12

MAYBE_UNUSED INLINE_HINT static ScriptType val_type(const ScriptVal value) {
  return (ScriptType)value.bytes[val_type_byte_index];
}

MAYBE_UNUSED INLINE_HINT static bool val_type_check(const ScriptVal v, const ScriptMask mask) {
  return (mask & (1 << val_type(v))) != 0;
}

MAYBE_UNUSED INLINE_HINT static ScriptVal val_null(void) {
  ASSERT(ScriptType_Null == 0, "ScriptType_Null should be initializable using zero-init");
  return (ScriptVal){0};
}

MAYBE_UNUSED INLINE_HINT static ScriptVal val_num(const f64 value) {
  ScriptVal result;
  *(f64*)result.bytes               = value;
  result.bytes[val_type_byte_index] = ScriptType_Num;
  return result;
}

MAYBE_UNUSED INLINE_HINT static ScriptVal val_bool(const bool value) {
  ScriptVal result;
  *(bool*)result.bytes              = value;
  result.bytes[val_type_byte_index] = ScriptType_Bool;
  return result;
}

MAYBE_UNUSED INLINE_HINT static ScriptVal val_vec3(const GeoVector value) {
  ScriptVal result;
  *(GeoVector*)result.bytes         = value;
  result.bytes[val_type_byte_index] = ScriptType_Vec3;
  return result;
}

MAYBE_UNUSED INLINE_HINT static ScriptVal val_quat(const GeoQuat q) {
  GeoQuat qNorm = geo_quat_norm_or_ident(q);
  if (qNorm.w < 0.0f) {
    /**
     * Due to having to store the type-tag we cannot use the full 128 bits for the quaternion,
     * luckily for unit quaternions we can reconstruct the magnitude of the W component from the
     * other components. Because we cannot reconstruct the sign of the W component we make sure the
     * W component is always positive.
     */
    qNorm = geo_quat_flip(qNorm);
  }
  ScriptVal result;
  *(GeoQuat*)result.bytes           = qNorm;
  result.bytes[val_type_byte_index] = ScriptType_Quat;
  return result;
}

MAYBE_UNUSED INLINE_HINT static ScriptVal val_color(const GeoColor value) {
  ScriptVal result;
  f16* restrict resultComps = (f16*)result.bytes;
  geo_color_pack_f16(value, resultComps);
  result.bytes[val_type_byte_index] = ScriptType_Color;
  return result;
}

MAYBE_UNUSED INLINE_HINT static ScriptVal val_entity(const EcsEntityId value) {
  ScriptVal result;
  *(EcsEntityId*)result.bytes       = value;
  result.bytes[val_type_byte_index] = ScriptType_Entity;
  return result;
}

MAYBE_UNUSED INLINE_HINT static ScriptVal val_str(const StringHash value) {
  ScriptVal result;
  *(StringHash*)result.bytes        = value;
  result.bytes[val_type_byte_index] = ScriptType_Str;
  return result;
}

MAYBE_UNUSED INLINE_HINT static f64 val_as_num(const ScriptVal value) { return *(f64*)value.bytes; }

MAYBE_UNUSED INLINE_HINT static bool val_as_bool(const ScriptVal value) {
  return *(bool*)value.bytes;
}

MAYBE_UNUSED INLINE_HINT static GeoVector val_as_vec3_dirty_w(const ScriptVal value) {
  return *(GeoVector*)value.bytes;
}

MAYBE_UNUSED INLINE_HINT static GeoVector val_as_vec3(const ScriptVal value) {
  GeoVector result = val_as_vec3_dirty_w(value);
  result.w         = 0.0f; // W value is aliased with the type tag.
  return result;
}

MAYBE_UNUSED INLINE_HINT static GeoQuat val_as_quat(const ScriptVal value) {
  GeoQuat   result = *(GeoQuat*)value.bytes;
  const f32 sum    = result.x * result.x + result.y * result.y + result.z * result.z;
  result.w         = intrinsic_sqrt_f32(1.0f - sum);
  return result;
}

MAYBE_UNUSED INLINE_HINT static GeoColor val_as_color(const ScriptVal value) {
  f16* restrict comps = (f16*)value.bytes;

  GeoColor result;
  result.r = float_f16_to_f32(comps[0]);
  result.g = float_f16_to_f32(comps[1]);
  result.b = float_f16_to_f32(comps[2]);
  result.a = float_f16_to_f32(comps[3]);
  return result;
}

MAYBE_UNUSED INLINE_HINT static EcsEntityId val_as_entity(const ScriptVal value) {
  return *(EcsEntityId*)value.bytes;
}

MAYBE_UNUSED INLINE_HINT static StringHash val_as_str(const ScriptVal value) {
  return *(StringHash*)value.bytes;
}
