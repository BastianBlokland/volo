#pragma once
#include "core.h"
#include "core_bits.h"
#include "ecs.h"
#include "geo.h"

typedef enum eScriptType {
  ScriptType_Null,
  ScriptType_Num,
  ScriptType_Bool,
  ScriptType_Vec3,
  ScriptType_Quat,
  ScriptType_Color,
  ScriptType_Entity,
  ScriptType_Str,

  ScriptType_Count,
} ScriptType;

typedef u16 ScriptMask;
ASSERT(ScriptType_Count < 16, "ScriptType's have to be indexable with 16 bits");

#define script_mask(_TYPE_) ((ScriptMask)(1 << _TYPE_))
#define script_mask_none ((ScriptMask)0)
#define script_mask_any ((ScriptMask)bit_range_32(0, ScriptType_Count))
#define script_mask_null script_mask(ScriptType_Null)
#define script_mask_num script_mask(ScriptType_Num)
#define script_mask_bool script_mask(ScriptType_Bool)
#define script_mask_vec3 script_mask(ScriptType_Vec3)
#define script_mask_quat script_mask(ScriptType_Quat)
#define script_mask_color script_mask(ScriptType_Color)
#define script_mask_entity script_mask(ScriptType_Entity)
#define script_mask_str script_mask(ScriptType_Str)
#define script_mask_time script_mask(ScriptType_Num)

/**
 * Type-erased script value.
 */
typedef struct sScriptVal {
  ALIGNAS(16) u8 bytes[16];
} ScriptVal;

ASSERT(sizeof(ScriptVal) == 16, "Expected ScriptVal's size to be 128 bits");
ASSERT(alignof(ScriptVal) == 16, "Expected ScriptVal's alignment to be 128 bits");

/**
 * Retrieve the type of the given value.
 */
ScriptType script_type(ScriptVal);
bool       script_type_check(ScriptVal, ScriptMask);

/**
 * Type-erase a value into a ScriptVal.
 */
ScriptVal script_null(void);
ScriptVal script_num(f64);
ScriptVal script_bool(bool);
ScriptVal script_vec3(GeoVector);
ScriptVal script_vec3_lit(f32 x, f32 y, f32 z);
ScriptVal script_quat(GeoQuat);
ScriptVal script_color(GeoColor);
ScriptVal script_entity(EcsEntityId);
ScriptVal script_entity_or_null(EcsEntityId);
ScriptVal script_str(StringHash);
ScriptVal script_str_empty(void);
ScriptVal script_str_or_null(StringHash);
ScriptVal script_time(TimeDuration); // Stored as seconds in a number value.

/**
 * Extract a specific type.
 */
f64          script_get_num(ScriptVal, f64 fallback);
bool         script_get_bool(ScriptVal, bool fallback);
GeoVector    script_get_vec3(ScriptVal, GeoVector fallback);
GeoQuat      script_get_quat(ScriptVal, GeoQuat fallback);
GeoColor     script_get_color(ScriptVal, GeoColor fallback);
EcsEntityId  script_get_entity(ScriptVal, EcsEntityId fallback);
StringHash   script_get_str(ScriptVal, StringHash fallback);
TimeDuration script_get_time(ScriptVal, TimeDuration fallback);

/**
 * Value utilities.
 */
bool      script_val_valid(ScriptVal);
bool      script_truthy(ScriptVal);
ScriptVal script_truthy_as_val(ScriptVal);
bool      script_falsy(ScriptVal);
ScriptVal script_falsy_as_val(ScriptVal);
bool      script_non_null(ScriptVal);
ScriptVal script_non_null_as_val(ScriptVal);
ScriptVal script_val_or(ScriptVal value, ScriptVal fallback);
u32       script_hash(ScriptVal);

/**
 * Create a textual representation of a value.
 */
String     script_val_type_str(ScriptType);
StringHash script_val_type_hash(ScriptType);
ScriptType script_val_type_from_hash(StringHash);
void       script_val_write(ScriptVal, DynString*);
String     script_val_scratch(ScriptVal);
void       script_mask_write(ScriptMask, DynString*);
String     script_mask_scratch(ScriptMask);

/**
 * Compare values.
 */
bool      script_val_equal(ScriptVal, ScriptVal);
ScriptVal script_val_equal_as_val(ScriptVal, ScriptVal);
bool      script_val_less(ScriptVal, ScriptVal);
ScriptVal script_val_less_as_val(ScriptVal, ScriptVal);
bool      script_val_greater(ScriptVal, ScriptVal);
ScriptVal script_val_greater_as_val(ScriptVal, ScriptVal);

/**
 * Value arithmetic.
 */
ScriptVal script_val_type(ScriptVal);
ScriptVal script_val_hash(ScriptVal);
ScriptVal script_val_neg(ScriptVal);
ScriptVal script_val_inv(ScriptVal);
ScriptVal script_val_add(ScriptVal, ScriptVal);
ScriptVal script_val_sub(ScriptVal, ScriptVal);
ScriptVal script_val_mul(ScriptVal, ScriptVal);
ScriptVal script_val_div(ScriptVal, ScriptVal);
ScriptVal script_val_mod(ScriptVal, ScriptVal);
ScriptVal script_val_dist(ScriptVal, ScriptVal);
ScriptVal script_val_norm(ScriptVal);
ScriptVal script_val_mag(ScriptVal);
ScriptVal script_val_abs(ScriptVal);
ScriptVal script_val_angle(ScriptVal, ScriptVal);
ScriptVal script_val_sin(ScriptVal);
ScriptVal script_val_cos(ScriptVal);
ScriptVal script_val_random(void);
ScriptVal script_val_random_sphere(void);
ScriptVal script_val_random_circle_xz(void);
ScriptVal script_val_random_between(ScriptVal, ScriptVal);
ScriptVal script_val_round_down(ScriptVal);
ScriptVal script_val_round_nearest(ScriptVal);
ScriptVal script_val_round_up(ScriptVal);
ScriptVal script_val_clamp(ScriptVal, ScriptVal min, ScriptVal max);
ScriptVal script_val_lerp(ScriptVal x, ScriptVal y, ScriptVal t);
ScriptVal script_val_min(ScriptVal, ScriptVal);
ScriptVal script_val_max(ScriptVal, ScriptVal);
ScriptVal script_val_perlin3(ScriptVal pos);

/**
 * Value conversions.
 */
ScriptVal script_val_vec3_compose(ScriptVal x, ScriptVal y, ScriptVal z);
ScriptVal script_val_vec_x(ScriptVal);
ScriptVal script_val_vec_y(ScriptVal);
ScriptVal script_val_vec_z(ScriptVal);
ScriptVal script_val_quat_from_euler(ScriptVal x, ScriptVal y, ScriptVal z);
ScriptVal script_val_quat_from_angle_axis(ScriptVal angle, ScriptVal axis);
ScriptVal script_val_color_r(ScriptVal);
ScriptVal script_val_color_g(ScriptVal);
ScriptVal script_val_color_b(ScriptVal);
ScriptVal script_val_color_a(ScriptVal);
ScriptVal script_val_color_compose(ScriptVal r, ScriptVal g, ScriptVal b, ScriptVal a);
ScriptVal script_val_color_compose_hsv(ScriptVal h, ScriptVal s, ScriptVal v, ScriptVal a);
ScriptVal script_val_color_for_val(ScriptVal);

/**
 * Create a formatting argument for a script value.
 */
#define script_val_fmt(_VAL_) fmt_text(script_val_scratch(_VAL_))
#define script_mask_fmt(_MASK_) fmt_text(script_mask_scratch(_MASK_))
