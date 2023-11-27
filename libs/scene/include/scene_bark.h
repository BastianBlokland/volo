#pragma once
#include "ecs_module.h"

typedef enum {
  SceneBarkType_Death,
  SceneBarkType_Confirm,

  SceneBarkType_Count,
} SceneBarkType;

ecs_comp_extern_public(SceneBarkComp) {
  u32        requests;
  i32        priority;
  StringHash tauntPrefabs[SceneBarkType_Count];
};

void scene_bark_request(SceneBarkComp*, SceneBarkType);
