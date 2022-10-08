#pragma once
#include "core_time.h"
#include "ecs_entity.h"
#include "ecs_module.h"

typedef enum {
  SceneAttackFlags_Firing = 1 << 0,
} SceneAttackFlags;

ecs_comp_extern_public(SceneAttackComp) {
  SceneAttackFlags flags;
  TimeDuration     lastFireTime;
  f32              aimNorm; // Process of aiming.

  TimeDuration interval;
  EcsEntityId  targetEntity;
  EcsEntityId  muzzleFlashVfx;
  EcsEntityId  projectileVfx;
};
