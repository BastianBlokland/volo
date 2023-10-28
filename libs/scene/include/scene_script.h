#pragma once
#include "ecs_entity.h"
#include "ecs_module.h"

// Forward declare from 'core_time.h'.
typedef i64 TimeDuration;

typedef enum {
  SceneScriptFlags_None            = 0,
  SceneScriptFlags_PauseEvaluation = 1 << 0,
} SceneScriptFlags;

typedef struct {
  u32          executedExprs;
  TimeDuration executedDur;
} SceneScriptStats;

ecs_comp_extern(SceneScriptComp);

/**
 * Query and update the scripts's configuration flags.
 */
SceneScriptFlags scene_script_flags(const SceneScriptComp*);
void             scene_script_flags_set(SceneScriptComp*, SceneScriptFlags);
void             scene_script_flags_unset(SceneScriptComp*, SceneScriptFlags);
void             scene_script_flags_toggle(SceneScriptComp*, SceneScriptFlags);

/**
 * Retrieve statistics for the given script.
 */
bool                    scene_script_did_panic(const SceneScriptComp*);
EcsEntityId             scene_script_asset(const SceneScriptComp*);
const SceneScriptStats* scene_script_stats(const SceneScriptComp*);

/**
 * Add a new script to the entity.
 */
SceneScriptComp* scene_script_add(EcsWorld*, EcsEntityId entity, EcsEntityId scriptAsset);
