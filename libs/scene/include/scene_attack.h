#pragma once
#include "core_time.h"
#include "ecs_entity.h"
#include "ecs_module.h"

ecs_comp_extern_public(SceneWeaponComp) {
  TimeDuration intervalMin, intervalMax;
  f32          spreadAngleMax;
  StringHash   animAim, animFire;
  StringHash   jointOrigin; // For example the weapon's muzzle.
  EcsEntityId  vfxFire, vfxProjectile, vfxImpact;
};

typedef enum {
  SceneAttackFlags_Firing = 1 << 0,
} SceneAttackFlags;

ecs_comp_extern_public(SceneAttackComp) {
  SceneAttackFlags flags;
  TimeDuration     lastFireTime, nextFireTime;
  f32              aimNorm; // Process of aiming.

  EcsEntityId targetEntity;
};
