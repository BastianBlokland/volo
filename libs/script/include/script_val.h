#pragma once
#include "core_dynstring.h"
#include "ecs_entity.h"
#include "geo_vector.h"

// Forward declare from 'core_time.h'.
typedef i64 TimeDuration;

typedef enum {
  ScriptType_Null,
  ScriptType_Number,
  ScriptType_Bool,
  ScriptType_Vector3,
  ScriptType_Entity,

  ScriptType_Count,
} ScriptType;

/**
 * Type-erased knowledge value.
 */
typedef struct {
  ALIGNAS(16) u32 data[4];
} ScriptVal;

ASSERT(sizeof(ScriptVal) == 16, "Expected ScriptVal's size to be 128 bits");
ASSERT(alignof(ScriptVal) == 16, "Expected ScriptVal's alignment to be 128 bits");

/**
 * Retrieve the type of the given value.
 */
ScriptType script_type(ScriptVal);

/**
 * Type-erase a value into a ScriptVal.
 */
ScriptVal script_null();
ScriptVal script_number(f64);
ScriptVal script_bool(bool);
ScriptVal script_vector3(GeoVector);
ScriptVal script_vector3_lit(f32 x, f32 y, f32 z);
ScriptVal script_entity(EcsEntityId);
ScriptVal script_time(TimeDuration); // Stored as seconds in a number value.

/**
 * Extract a specific type.
 */
f64          script_get_number(ScriptVal, f64 fallback);
bool         script_get_bool(ScriptVal, bool fallback);
GeoVector    script_get_vector3(ScriptVal, GeoVector fallback);
EcsEntityId  script_get_entity(ScriptVal, EcsEntityId fallback);
TimeDuration script_get_time(ScriptVal, TimeDuration fallback);

/**
 * Value utilities.
 */
bool      script_val_has(ScriptVal);
ScriptVal script_val_or(ScriptVal value, ScriptVal fallback);

/**
 * Create a textual representation of a value.
 */
String script_val_type_str(ScriptType);
void   script_val_str_write(ScriptVal, DynString*);
String script_val_str_scratch(ScriptVal);

/**
 * Compare values.
 */
bool script_val_equal(ScriptVal, ScriptVal);
bool script_val_less(ScriptVal, ScriptVal);
bool script_val_greater(ScriptVal, ScriptVal);

/**
 * Value arithmetic.
 */
ScriptVal script_val_add(ScriptVal, ScriptVal);
ScriptVal script_val_sub(ScriptVal, ScriptVal);

/**
 * Create a formatting argument for a script value.
 */
#define script_val_fmt(_VAL_) fmt_text(script_val_str_scratch(_VAL_))
