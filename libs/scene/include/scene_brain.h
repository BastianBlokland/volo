#pragma once
#include "ecs_entity.h"
#include "ecs_module.h"

// Forward declare from 'ai_blackboard.h'.
typedef struct sAiBlackboard AiBlackboard;

ecs_comp_extern(SceneBrainComp);

/**
 * Get access to the brain's blackboard for querying / storing knowledge.
 */
const AiBlackboard* scene_brain_blackboard(const SceneBrainComp*);
AiBlackboard*       scene_brain_blackboard_mutable(SceneBrainComp*);

/**
 * Add a new brain to the entity that executes the given behavior asset.
 */
void scene_brain_add(EcsWorld*, EcsEntityId entity, EcsEntityId behaviorAsset);
