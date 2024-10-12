#pragma once
#include "ecs_entity.h"
#include "ecs_module.h"
#include "geo_color.h"
#include "geo_quat.h"
#include "geo_ray.h"

// Forward declare from 'core_time.h'.
typedef i64 TimeDuration;

// Forward declare from 'script_panic.h'.
typedef struct sScriptPanic ScriptPanic;

typedef enum {
  SceneScriptFlags_None            = 0,
  SceneScriptFlags_DidPanic        = 1 << 0,
  SceneScriptFlags_PauseEvaluation = 1 << 1,
} SceneScriptFlags;

typedef struct {
  u32          executedOps;
  TimeDuration executedDur;
} SceneScriptStats;

/**
 * SceneScriptComp's support multiple slots for executing scripts, this can be used to execute
 * multiple scripts on the same entity.
 */
typedef u8 SceneScriptSlot;

ecs_comp_extern(SceneScriptEnvComp);
ecs_comp_extern(SceneScriptComp);

/**
 * Query and update the scripts's flags.
 */
SceneScriptFlags scene_script_flags(const SceneScriptComp*);
void             scene_script_flags_set(SceneScriptComp*, SceneScriptFlags);
void             scene_script_flags_unset(SceneScriptComp*, SceneScriptFlags);
void             scene_script_flags_toggle(SceneScriptComp*, SceneScriptFlags);

/**
 * Retrieve statistics for a specific script slot.
 */
u32                     scene_script_count(const SceneScriptComp*);
EcsEntityId             scene_script_asset(const SceneScriptComp*, SceneScriptSlot);
const ScriptPanic*      scene_script_panic(const SceneScriptComp*, SceneScriptSlot);
const SceneScriptStats* scene_script_stats(const SceneScriptComp*, SceneScriptSlot);

typedef enum {
  SceneScriptDebugType_Line,
  SceneScriptDebugType_Sphere,
  SceneScriptDebugType_Box,
  SceneScriptDebugType_Arrow,
  SceneScriptDebugType_Orientation,
  SceneScriptDebugType_Text,
  SceneScriptDebugType_Trace,
} SceneScriptDebugType;

typedef struct {
  GeoVector start, end;
  GeoColor  color;
} SceneScriptDebugLine;

typedef struct {
  GeoVector pos;
  GeoColor  color;
  f32       radius;
} SceneScriptDebugSphere;

typedef struct {
  GeoVector pos;
  GeoQuat   rot;
  GeoVector size;
  GeoColor  color;
} SceneScriptDebugBox;

typedef struct {
  GeoVector start, end;
  GeoColor  color;
  f32       radius;
} SceneScriptDebugArrow;

typedef struct {
  GeoVector pos;
  GeoQuat   rot;
  f32       size;
} SceneScriptDebugOrientation;

typedef struct {
  GeoVector pos;
  GeoColor  color;
  String    text;
  u16       fontSize;
} SceneScriptDebugText;

typedef struct {
  String text;
} SceneScriptDebugTrace;

typedef struct {
  SceneScriptDebugType type;
  SceneScriptSlot      slot;
  union {
    SceneScriptDebugLine        data_line;
    SceneScriptDebugSphere      data_sphere;
    SceneScriptDebugBox         data_box;
    SceneScriptDebugArrow       data_arrow;
    SceneScriptDebugOrientation data_orientation;
    SceneScriptDebugText        data_text;
    SceneScriptDebugTrace       data_trace;
  };
} SceneScriptDebug;

const SceneScriptDebug* scene_script_debug_data(const SceneScriptComp*);
usize                   scene_script_debug_count(const SceneScriptComp*);
void                    scene_script_debug_ray_update(SceneScriptEnvComp*, GeoRay);

/**
 * Setup a script on the given entity.
 */
SceneScriptComp* scene_script_add(
    EcsWorld*, EcsEntityId entity, const EcsEntityId scriptAssets[], u32 scriptAssetCount);
