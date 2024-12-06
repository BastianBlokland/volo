#pragma once
#include "core_time.h"
#include "ecs_entity.h"
#include "ecs_module.h"
#include "geo_quat.h"
#include "scene.h"

typedef enum {
  SceneAttackFlags_Firing   = 1 << 0,
  SceneAttackFlags_Readying = 1 << 1,
  SceneAttackFlags_Trace    = 1 << 2, // Enable diagnostic tracing.
} SceneAttackFlags;

ecs_comp_extern_public(SceneAttackComp) {
  StringHash       weaponName;
  SceneAttackFlags flags : 16;
  u16              executedEffects;
  f32              readyNorm; // Process of readying the weapon, 1.0 = ready.
  TimeDuration     lastHasTargetTime;
  TimeDuration     lastFireTime, nextFireTime;
  EcsEntityId      targetCurrent, targetDesired;
  GeoVector        targetPos;
};

ecs_comp_extern_public(SceneAttackAimComp) {
  StringHash aimJoint;
  f32        aimSpeedRad; // Radians per second.
  bool       isAiming;
  GeoQuat    aimLocalActual, aimLocalTarget;
};

/**
 * Compute the world-space aim rotation.
 * NOTE: 'SceneAttackAimComp' is optional, when null the raw transform rotation is returned.
 */
GeoQuat   scene_attack_aim_rot(const SceneTransformComp*, const SceneAttackAimComp*);
GeoVector scene_attack_aim_dir(const SceneTransformComp*, const SceneAttackAimComp*);

/**
 * Start aiming in the given direction.
 */
void scene_attack_aim(SceneAttackAimComp*, const SceneTransformComp*, GeoVector direction);
void scene_attack_aim_reset(SceneAttackAimComp*);

typedef enum {
  SceneAttackEventType_Proj,
  SceneAttackEventType_DmgSphere,
  SceneAttackEventType_DmgFrustum,
} SceneAttackEventType;

typedef struct {
  GeoVector pos, target;
} SceneAttackEventProj;

typedef struct {
  GeoVector pos;
  f32       radius;
} SceneAttackEventDmgSphere;

typedef struct {
  GeoVector corners[8];
} SceneAttackEventDmgFrustum;

typedef struct {
  SceneAttackEventType type;
  TimeDuration         expireTimestamp;
  union {
    SceneAttackEventProj       data_proj;
    SceneAttackEventDmgSphere  data_dmgSphere;
    SceneAttackEventDmgFrustum data_dmgFrustum;
  };
} SceneAttackEvent;

ecs_comp_extern(SceneAttackTraceComp);

const SceneAttackEvent* scene_attack_trace_begin(const SceneAttackTraceComp*);
const SceneAttackEvent* scene_attack_trace_end(const SceneAttackTraceComp*);
