#pragma once
#include "core_time.h"
#include "ecs_entity.h"
#include "ecs_module.h"

ecs_comp_extern_public(SceneWeaponComp) {
  StringHash  animFire;
  StringHash  jointOrigin; // For example the weapon's muzzle.
  EcsEntityId vfxFire, vfxImpact;

  struct {
    EcsEntityId  vfx;
    TimeDuration delay;
    f32          spreadAngleMax;
    f32          speed;
    f32          damage;
  } projectile;
};

typedef enum {
  SceneAttackFlags_Firing = 1 << 0,
} SceneAttackFlags;

ecs_comp_extern_public(SceneAttackComp) {
  StringHash       weaponName;
  SceneAttackFlags flags;
  f32              aimNorm; // Process of aiming.
  TimeDuration     lastFireTime, nextFireTime;
  EcsEntityId      targetEntity;
};
