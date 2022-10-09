#pragma once
#include "core_time.h"
#include "ecs_entity.h"
#include "ecs_module.h"

ecs_comp_extern_public(SceneProjectileComp) {
  TimeDuration delay;
  f32          speed;
  f32          damage;
  EcsEntityId  instigator;
  EcsEntityId  impactVfx;
};
