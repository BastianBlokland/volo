#pragma once
#include "core_time.h"
#include "ecs_module.h"

ecs_comp_extern_public(SceneExplosiveComp) {
  TimeDuration delay;
  f32          radius, damage;
};
