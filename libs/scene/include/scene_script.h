#pragma once
#include "ecs_entity.h"
#include "ecs_module.h"

typedef enum {
  SceneScriptFlags_None            = 0,
  SceneScriptFlags_PauseEvaluation = 1 << 0,
} SceneScriptFlags;

ecs_comp_extern(SceneScriptComp);

/**
 * Query and update the scripts's configuration flags.
 */
SceneScriptFlags scene_script_flags(const SceneScriptComp*);
void             scene_script_flags_set(SceneScriptComp*, SceneScriptFlags);
void             scene_script_flags_unset(SceneScriptComp*, SceneScriptFlags);
void             scene_script_flags_toggle(SceneScriptComp*, SceneScriptFlags);

/**
 * Retrieve the asset for the given script.
 */
EcsEntityId scene_script_asset(const SceneScriptComp*);

/**
 * Add a new script to the entity.
 */
SceneScriptComp* scene_script_add(EcsWorld*, EcsEntityId entity, EcsEntityId scriptAsset);
