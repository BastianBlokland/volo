#pragma once
#include "core_time.h"
#include "ecs_module.h"

ecs_comp_extern_public(SceneHealthComp) {
  f32          norm;
  f32          max;
  f32          damage;
  TimeDuration lastDamagedTime;
};

void scene_health_damage(SceneHealthComp*, f32 amount);
