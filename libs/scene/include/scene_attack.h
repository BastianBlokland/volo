#pragma once
#include "core_time.h"
#include "ecs_entity.h"
#include "ecs_module.h"
#include "geo_quat.h"

// Forward declare from 'scene_transform.h'.
ecs_comp_extern(SceneTransformComp);

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

/**
 * Compute the world-space aim rotation.
 * NOTE: 'SceneAttackAimComp' is optional, when null the raw transform rotation is returned.
 */
GeoQuat   scene_attack_aim_rot(const SceneTransformComp*, const SceneAttackAimComp*);
GeoVector scene_attack_aim_dir(const SceneTransformComp*, const SceneAttackAimComp*);
