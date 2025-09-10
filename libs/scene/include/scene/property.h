#pragma once
#include "ecs/module.h"
#include "script/forward.h"

ecs_comp_extern(ScenePropertyComp);

/**
 * Query and update properties.
 */
ScriptVal        scene_prop_load(const ScenePropertyComp*, StringHash key);
void             scene_prop_store(ScenePropertyComp*, StringHash key, ScriptVal);
void             scene_prop_clear(ScenePropertyComp*);
const ScriptMem* scene_prop_memory(const ScenePropertyComp*);
ScriptMem*       scene_prop_memory_mut(ScenePropertyComp*);

/**
 * Add a property component to the given entity.
 */
ScenePropertyComp* scene_prop_add(EcsWorld*, EcsEntityId);
