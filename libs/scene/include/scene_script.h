#pragma once
#include "ecs_entity.h"
#include "ecs_module.h"
#include "script.h"

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

/**
 * Setup a script on the given entity.
 */
SceneScriptComp* scene_script_add(
    EcsWorld*, EcsEntityId entity, const EcsEntityId scriptAssets[], u32 scriptAssetCount);
