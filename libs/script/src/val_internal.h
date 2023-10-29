#pragma once
#include "core_intrinsic.h"
#include "geo_quat.h"
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
 * | Quat    | f32 q1        | f32 q2        | f32 q3     | type tag (4) |
 * | Entity  | lower 32 bits | upper 32 bits | unused     | type tag (5) |
 * | String  | u32           | unused        | unused     | type tag (6) |
 *
 * NOTE: Only unit quaternions are supported (as the 4th component is reconstructed).
 * NOTE: Assumes little-endian byte order.
 */

MAYBE_UNUSED INLINE_HINT static ScriptVal val_null() {
  ASSERT(ScriptType_Null == 0, "ScriptType_Null should be initializable using zero-init");
  return (ScriptVal){0};
}

MAYBE_UNUSED INLINE_HINT static ScriptVal val_number(const f64 value) {
  ScriptVal result;
  *(f64*)result.data = value;
  result.data[3]     = ScriptType_Number;
  return result;
}

MAYBE_UNUSED INLINE_HINT static ScriptVal val_bool(const bool value) {
  ScriptVal result;
  *(bool*)result.data = value;
  result.data[3]      = ScriptType_Bool;
  return result;
}

MAYBE_UNUSED INLINE_HINT static ScriptVal val_vector3(const GeoVector value) {
  ScriptVal result;
  *(GeoVector*)result.data = value;
  result.data[3]           = ScriptType_Vector3;
  return result;
}

MAYBE_UNUSED INLINE_HINT static ScriptVal val_quat(const GeoQuat q) {
  ScriptVal result;
  *(GeoQuat*)result.data = geo_quat_norm_or_ident(q);
  result.data[3]         = ScriptType_Quat;
  return result;
}

MAYBE_UNUSED INLINE_HINT static ScriptVal val_entity(const EcsEntityId value) {
  ScriptVal result;
  *(EcsEntityId*)result.data = value;
  result.data[3]             = ScriptType_Entity;
  return result;
}

MAYBE_UNUSED INLINE_HINT static f64 val_as_number(const ScriptVal value) {
  return *(f64*)value.data;
}

MAYBE_UNUSED INLINE_HINT static bool val_as_bool(const ScriptVal value) {
  return *(bool*)value.data;
}

MAYBE_UNUSED INLINE_HINT static GeoVector val_as_vector3_dirty_w(const ScriptVal value) {
  return *(GeoVector*)value.data;
}

MAYBE_UNUSED INLINE_HINT static GeoVector val_as_vector3(const ScriptVal value) {
  GeoVector result = val_as_vector3_dirty_w(value);
  result.w         = 0.0f; // W value is aliased with the type tag.
  return result;
}

MAYBE_UNUSED INLINE_HINT static GeoQuat val_as_quat(const ScriptVal value) {
  GeoQuat   result = *(GeoQuat*)value.data;
  const f32 sum    = result.x * result.x + result.y * result.y + result.z * result.z;
  result.w         = intrinsic_sqrt_f32(1.0f - sum);
  return result;
}

MAYBE_UNUSED INLINE_HINT static EcsEntityId val_as_entity(const ScriptVal value) {
  return *(EcsEntityId*)value.data;
}

MAYBE_UNUSED INLINE_HINT static StringHash val_as_string(const ScriptVal value) {
  return *(StringHash*)value.data;
}
