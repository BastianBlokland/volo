#pragma once
#include "ecs_entity.h"
#include "ecs_module.h"

// Forward declare from 'ai_blackboard.h'.
typedef struct sAiBlackboard AiBlackboard;

typedef enum {
  SceneBrainFlags_None         = 0,
  SceneBrainFlags_PauseSensors = 1 << 1,
} SceneBrainFlags;

ecs_comp_extern(SceneBrainComp);

/**
 * Get access to the brain's blackboard for querying / storing knowledge.
 */
const AiBlackboard* scene_brain_blackboard(const SceneBrainComp*);
AiBlackboard*       scene_brain_blackboard_mutable(SceneBrainComp*);

/**
 * Query and update the brain's configuration flags.
 */
SceneBrainFlags scene_brain_flags(const SceneBrainComp*);
void            scene_brain_flags_set(SceneBrainComp*, SceneBrainFlags);
void            scene_brain_flags_unset(SceneBrainComp*, SceneBrainFlags);
void            scene_brain_flags_toggle(SceneBrainComp*, SceneBrainFlags);

/**
 * Add a new brain to the entity that executes the given behavior asset.
 */
SceneBrainComp* scene_brain_add(EcsWorld*, EcsEntityId entity, EcsEntityId behaviorAsset);
