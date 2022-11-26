#pragma once
#include "core_time.h"
#include "ecs_entity.h"
#include "ecs_module.h"
#include "geo_quat.h"

typedef enum {
  SceneAttackFlags_Firing = 1 << 0,
} SceneAttackFlags;

ecs_comp_extern_public(SceneAttackComp) {
  StringHash       weaponName;
  SceneAttackFlags flags : 16;
  u16              executedEffects;
  f32              readyNorm; // Process of readying the weapon, 1.0 = ready.
  TimeDuration     lastFireTime, nextFireTime;
  EcsEntityId      targetEntity;
  GeoVector        targetPos;
};

ecs_comp_extern_public(SceneAttackAimComp) {
  StringHash aimJoint;
  f32        aimSpeedRad; // Radians per second.
  GeoQuat    aimRotLocal;
};
