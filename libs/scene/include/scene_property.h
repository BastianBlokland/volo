#pragma once
#include "ecs_module.h"
#include "script.h"

ecs_comp_extern(ScenePropertyComp);

/**
 * Query and update knowledge.
 */
ScriptVal        scene_knowledge_load(const ScenePropertyComp*, StringHash key);
void             scene_knowledge_store(ScenePropertyComp*, StringHash key, ScriptVal);
const ScriptMem* scene_knowledge_memory(const ScenePropertyComp*);
ScriptMem*       scene_knowledge_memory_mut(ScenePropertyComp*);

/**
 * Add a knowledge component to the given entity.
 */
ScenePropertyComp* scene_knowledge_add(EcsWorld*, EcsEntityId);
