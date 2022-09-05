#pragma once
#include "ecs_module.h"

ecs_comp_extern_public(SceneHealthComp) {
  f32 norm;
  f32 max;
};

void scene_health_damage(SceneHealthComp*, f32 amount);
