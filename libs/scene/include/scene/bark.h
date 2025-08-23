#pragma once
#include "ecs/module.h"

typedef enum {
  SceneBarkType_Death,
  SceneBarkType_Confirm,

  SceneBarkType_Count,
} SceneBarkType;

ecs_comp_extern_public(SceneBarkComp) {
  u32        requests;
  i32        priority;
  StringHash barkPrefabs[SceneBarkType_Count];
};

String scene_bark_name(SceneBarkType);
void   scene_bark_request(SceneBarkComp*, SceneBarkType);
