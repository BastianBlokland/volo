#pragma once
#include "ecs_entity.h"
#include "ecs_module.h"

ecs_comp_extern_public(SceneProjectileComp) {
  f32         speed;
  f32         damage;
  EcsEntityId instigator;
};
