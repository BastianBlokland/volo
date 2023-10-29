#pragma once
#include "core_string.h"
#include "core_time.h"
#include "ecs_entity.h"
#include "geo_quat.h"

// Forward declare from 'script_val.h'.
typedef union uScriptVal ScriptVal;
typedef u16              ScriptMask;

// Forward declare from 'script_enum.h'.
typedef struct sScriptEnum ScriptEnum;

// Forward declare from 'script_error.h'.
typedef struct sScriptError ScriptError;

typedef struct {
  const ScriptVal* values;
  u16              count;
} ScriptArgs;

bool script_arg_check(ScriptArgs, u16 i, ScriptMask, ScriptError*);

f64          script_arg_number(ScriptArgs, u16 i, ScriptError*);
f64          script_arg_number_range(ScriptArgs, u16 i, f64 min, f64 max, ScriptError*);
bool         script_arg_bool(ScriptArgs, u16 i, ScriptError*);
GeoVector    script_arg_vector3(ScriptArgs, u16 i, ScriptError*);
GeoQuat      script_arg_quat(ScriptArgs, u16 i, ScriptError*);
EcsEntityId  script_arg_entity(ScriptArgs, u16 i, ScriptError*);
StringHash   script_arg_string(ScriptArgs, u16 i, ScriptError*);
TimeDuration script_arg_time(ScriptArgs, u16 i, ScriptError*);
i32          script_arg_enum(ScriptArgs, u16 i, const ScriptEnum*, ScriptError*);

f64         script_arg_opt_number(ScriptArgs, u16 i, f64 def, ScriptError*);
f64         script_arg_opt_number_range(ScriptArgs, u16 i, f64 min, f64 max, f64 def, ScriptError*);
bool        script_arg_opt_bool(ScriptArgs, u16 i, bool def, ScriptError*);
GeoVector   script_arg_opt_vector3(ScriptArgs, u16 i, GeoVector def, ScriptError*);
GeoQuat     script_arg_opt_quat(ScriptArgs, u16 i, GeoQuat def, ScriptError*);
EcsEntityId script_arg_opt_entity(ScriptArgs, u16 i, EcsEntityId def, ScriptError*);
StringHash  script_arg_opt_string(ScriptArgs, u16 i, StringHash def, ScriptError*);
TimeDuration script_arg_opt_time(ScriptArgs, u16 i, TimeDuration def, ScriptError*);
i32          script_arg_opt_enum(ScriptArgs, u16 i, const ScriptEnum*, i32 def, ScriptError*);

f64          script_arg_maybe_number(ScriptArgs, u16 i, f64 def);
bool         script_arg_maybe_bool(ScriptArgs, u16 i, bool def);
GeoVector    script_arg_maybe_vector3(ScriptArgs, u16 i, GeoVector def);
GeoQuat      script_arg_maybe_quat(ScriptArgs, u16 i, GeoQuat def);
EcsEntityId  script_arg_maybe_entity(ScriptArgs, u16 i, EcsEntityId def);
StringHash   script_arg_maybe_string(ScriptArgs, u16 i, StringHash def);
TimeDuration script_arg_maybe_time(ScriptArgs, u16 i, TimeDuration def);
i32          script_arg_maybe_enum(ScriptArgs, u16 i, const ScriptEnum*, i32 def);

ScriptVal script_arg_last_or_null(ScriptArgs);
