#pragma once
#include "ecs_module.h"

typedef enum {
  SceneTauntType_Death,

  SceneTauntType_Count,
} SceneTauntType;

ecs_comp_extern_public(SceneTauntComp) { StringHash tauntPrefabs[SceneTauntType_Count]; };
