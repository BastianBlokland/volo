#pragma once
#include "core_sentinel.h"
#include "ecs_module.h"

// Forward declare from 'scene_collision.h'.
typedef enum eSceneLayer SceneLayer;

typedef enum eSceneFaction {
  SceneFaction_A,
  SceneFaction_B,
  SceneFaction_C,
  SceneFaction_D,

  SceneFaction_Count,
  SceneFaction_None = sentinel_u32
} SceneFaction;

ecs_comp_extern_public(SceneFactionComp) { SceneFaction id; };

String     scene_faction_name(SceneFaction);
SceneLayer scene_faction_layers(SceneFaction);

bool scene_is_friendly(const SceneFactionComp*, const SceneFactionComp*);
bool scene_is_hostile(const SceneFactionComp*, const SceneFactionComp*);
