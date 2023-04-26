#pragma once
#include "ecs_module.h"

typedef enum {
  SceneTauntRequests_Death = 1 << 0,
} SceneTauntRequests;

typedef enum {
  SceneTauntType_Death,

  SceneTauntType_Count,
} SceneTauntType;

ecs_comp_extern_public(SceneTauntComp) {
  SceneTauntRequests requests;
  StringHash         tauntPrefabs[SceneTauntType_Count];
};
