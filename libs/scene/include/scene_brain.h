#pragma once
#include "ecs_entity.h"
#include "ecs_module.h"
#include "script_val.h"

// Forward declare from 'script_mem.h'.
typedef struct sScriptMem ScriptMem;

// Forward declare from 'ai_tracer_record.h'.
typedef struct sAiTracerRecord AiTracerRecord;

typedef enum {
  SceneBrainFlags_None             = 0,
  SceneBrainFlags_Trace            = 1 << 0,
  SceneBrainFlags_PauseEvaluation  = 1 << 1,
  SceneBrainFlags_PauseSensors     = 1 << 2,
  SceneBrainFlags_PauseControllers = 1 << 3,
} SceneBrainFlags;

ecs_comp_extern(SceneBrainComp);

/**
 * Query and update the brain's memory.
 */
ScriptVal        scene_brain_get(const SceneBrainComp*, StringHash key);
void             scene_brain_set(const SceneBrainComp*, StringHash key, ScriptVal);
void             scene_brain_set_null(const SceneBrainComp*, StringHash key);
const ScriptMem* scene_brain_memory(const SceneBrainComp*);

/**
 * Get access to the brain's tracer for debug visualization purposes.
 */
const AiTracerRecord* scene_brain_tracer(const SceneBrainComp*);

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
