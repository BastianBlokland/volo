#pragma once
#include "ecs_module.h"
#include "script.h"

ecs_comp_extern(SceneKnowledgeComp);

/**
 * Query and update knowledge.
 */
ScriptVal        scene_knowledge_load(const SceneKnowledgeComp*, StringHash key);
void             scene_knowledge_store(SceneKnowledgeComp*, StringHash key, ScriptVal);
const ScriptMem* scene_knowledge_memory(const SceneKnowledgeComp*);
ScriptMem*       scene_knowledge_memory_mut(SceneKnowledgeComp*);

/**
 * Add a knowledge component to the given entity.
 */
SceneKnowledgeComp* scene_knowledge_add(EcsWorld*, EcsEntityId);
