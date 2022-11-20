#pragma once
#include "core_time.h"
#include "ecs_module.h"

ecs_comp_extern_public(SceneSpawnerComp) {
  StringHash   prefabId;
  f32          radius;
  u32          maxInstances;
  TimeDuration intervalMin, intervalMax;
  TimeDuration nextTime;
};
