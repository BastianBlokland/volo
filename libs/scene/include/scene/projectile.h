#pragma once
#include "core/time.h"
#include "ecs/module.h"
#include "geo/vector.h"
#include "scene/forward.h"

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
