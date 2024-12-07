#pragma once
#include "core_time.h"
#include "ecs_module.h"
#include "geo_vector.h"
#include "scene.h"

typedef enum {
  SceneProjectile_Seek = 1 << 0,
} SceneProjectileFlags;

ecs_comp_extern_public(SceneProjectileComp) {
  SceneProjectileFlags flags : 8;
  SceneStatusMask      applyStatus;
  f32                  speed;
  f32                  damage, damageRadius;
  TimeDuration         age;
  TimeDuration         destroyDelay;
  StringHash           impactPrefab;
  EcsEntityId          instigator;
  EcsEntityId          seekEntity;
  GeoVector            seekPos;
};
