#pragma once
#include "core_string.h"
#include "core_time.h"
#include "ecs_entity.h"

// Forward declare from 'geo_quat.h'.
typedef union uGeoQuat GeoQuat;

// Forward declare from 'geo_color.h'.
typedef union uGeoColor GeoColor;

// Forward declare from 'geo_vector.h'.
typedef union uGeoVector GeoVector;

// Internal forward declarations:
typedef enum eScriptType         ScriptType;
typedef struct sScriptBinderCall ScriptBinderCall;
typedef struct sScriptEnum       ScriptEnum;
typedef struct sScriptVal        ScriptVal;
typedef u16                      ScriptMask;

/**
 * Argument check utilities.
 * NOTE: On failure the functions will not return, control-flow returns back to the script runtime.
 */

void script_arg_check(ScriptBinderCall*, u16 i, ScriptMask);
bool script_arg_has(ScriptBinderCall*, u16 i);

void script_arg_shift(ScriptBinderCall*);

ScriptVal    script_arg_any(ScriptBinderCall*, u16 i);
f64          script_arg_num(ScriptBinderCall*, u16 i);
f64          script_arg_num_range(ScriptBinderCall*, u16 i, f64 min, f64 max);
bool         script_arg_bool(ScriptBinderCall*, u16 i);
GeoVector    script_arg_vec3(ScriptBinderCall*, u16 i);
GeoQuat      script_arg_quat(ScriptBinderCall*, u16 i);
GeoColor     script_arg_color(ScriptBinderCall*, u16 i);
EcsEntityId  script_arg_entity(ScriptBinderCall*, u16 i);
StringHash   script_arg_str(ScriptBinderCall*, u16 i);
TimeDuration script_arg_time(ScriptBinderCall*, u16 i);
i32          script_arg_enum(ScriptBinderCall*, u16 i, const ScriptEnum*);

ScriptType   script_arg_opt_type(ScriptBinderCall*, u16 i);
f64          script_arg_opt_num(ScriptBinderCall*, u16 i, f64 def);
f64          script_arg_opt_num_range(ScriptBinderCall*, u16 i, f64 min, f64 max, f64 def);
bool         script_arg_opt_bool(ScriptBinderCall*, u16 i, bool def);
GeoVector    script_arg_opt_vec3(ScriptBinderCall*, u16 i, GeoVector def);
GeoQuat      script_arg_opt_quat(ScriptBinderCall*, u16 i, GeoQuat def);
GeoColor     script_arg_opt_color(ScriptBinderCall*, u16 i, GeoColor def);
EcsEntityId  script_arg_opt_entity(ScriptBinderCall*, u16 i, EcsEntityId def);
StringHash   script_arg_opt_str(ScriptBinderCall*, u16 i, StringHash def);
TimeDuration script_arg_opt_time(ScriptBinderCall*, u16 i, TimeDuration def);
i32          script_arg_opt_enum(ScriptBinderCall*, u16 i, const ScriptEnum*, i32 def);

f64          script_arg_maybe_num(ScriptBinderCall*, u16 i, f64 def);
bool         script_arg_maybe_bool(ScriptBinderCall*, u16 i, bool def);
GeoVector    script_arg_maybe_vec3(ScriptBinderCall*, u16 i, GeoVector def);
GeoQuat      script_arg_maybe_quat(ScriptBinderCall*, u16 i, GeoQuat def);
GeoColor     script_arg_maybe_color(ScriptBinderCall*, u16 i, GeoColor def);
EcsEntityId  script_arg_maybe_entity(ScriptBinderCall*, u16 i, EcsEntityId def);
StringHash   script_arg_maybe_str(ScriptBinderCall*, u16 i, StringHash def);
TimeDuration script_arg_maybe_time(ScriptBinderCall*, u16 i, TimeDuration def);
i32          script_arg_maybe_enum(ScriptBinderCall*, u16 i, const ScriptEnum*, i32 def);
