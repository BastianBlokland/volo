#pragma once
#include "core_string.h"
#include "core_time.h"
#include "ecs_entity.h"
#include "geo_quat.h"

// Forward declare from 'script_val.h'.
typedef union uScriptVal ScriptVal;

typedef struct {
  const ScriptVal* values;
  usize            count;
} ScriptArgs;

f64          script_arg_number(ScriptArgs, u32 i, f64 fallback);
bool         script_arg_bool(ScriptArgs, u32 i, bool fallback);
GeoVector    script_arg_vector3(ScriptArgs, u32 i, GeoVector fallback);
GeoQuat      script_arg_quat(ScriptArgs, u32 i, GeoQuat fallback);
EcsEntityId  script_arg_entity(ScriptArgs, u32 i, EcsEntityId fallback);
StringHash   script_arg_string(ScriptArgs, u32 i, StringHash fallback);
TimeDuration script_arg_time(ScriptArgs, u32 i, TimeDuration fallback);

ScriptVal script_arg_last_or_null(ScriptArgs);
