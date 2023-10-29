#pragma once
#include "core_string.h"
#include "core_time.h"
#include "ecs_entity.h"
#include "geo_quat.h"

// Forward declare from 'script_val.h'.
typedef union uScriptVal ScriptVal;

// Forward declare from 'script_enum.h'.
typedef struct sScriptEnum ScriptEnum;

typedef struct {
  const ScriptVal* values;
  usize            count;
} ScriptArgs;

f64          script_arg_maybe_number(ScriptArgs, u32 i, f64 def);
bool         script_arg_maybe_bool(ScriptArgs, u32 i, bool def);
GeoVector    script_arg_maybe_vector3(ScriptArgs, u32 i, GeoVector def);
GeoQuat      script_arg_maybe_quat(ScriptArgs, u32 i, GeoQuat def);
EcsEntityId  script_arg_maybe_entity(ScriptArgs, u32 i, EcsEntityId def);
StringHash   script_arg_maybe_string(ScriptArgs, u32 i, StringHash def);
TimeDuration script_arg_maybe_time(ScriptArgs, u32 i, TimeDuration def);
i32          script_arg_maybe_enum(ScriptArgs, u32 i, const ScriptEnum*, i32 def);

ScriptVal script_arg_last_or_null(ScriptArgs);
