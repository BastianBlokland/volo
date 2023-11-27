#pragma once
#include "ecs_module.h"

typedef enum {
  SceneTauntType_Death,
  SceneTauntType_Confirm,

  SceneTauntType_Count,
} SceneTauntType;

ecs_comp_extern_public(SceneTauntComp) {
  u32        requests;
  i32        priority;
  StringHash tauntPrefabs[SceneTauntType_Count];
};

void scene_taunt_request(SceneTauntComp*, SceneTauntType);
