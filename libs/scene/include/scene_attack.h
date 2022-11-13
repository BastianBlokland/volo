#pragma once
#include "core_time.h"
#include "ecs_entity.h"
#include "ecs_module.h"

typedef enum {
  SceneAttackFlags_Firing = 1 << 0,
} SceneAttackFlags;

ecs_comp_extern_public(SceneWeaponComp) {
  TimeDuration intervalMin, intervalMax;
  StringHash   animAim, animFire;
  EcsEntityId  vfxMuzzleFlash, vfxProjectile, vfxImpact;
  f32          spreadAngleMax;
};

ecs_comp_extern_public(SceneAttackComp) {
  SceneAttackFlags flags;
  TimeDuration     lastFireTime, nextFireTime;
  f32              aimNorm; // Process of aiming.

  EcsEntityId targetEntity;
};
