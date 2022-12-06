#pragma once
#include "core_time.h"
#include "ecs_entity.h"
#include "ecs_module.h"
#include "geo_vector.h"

typedef enum {
  SceneProjectile_Seek = 1 << 0,
} SceneProjectileFlags;

ecs_comp_extern_public(SceneProjectileComp) {
  SceneProjectileFlags flags;
  f32                  speed;
  f32                  damage, damageRadius;
  TimeDuration         age;
  TimeDuration         destroyDelay;
  TimeDuration         impactLifetime;
  EcsEntityId          instigator;
  EcsEntityId          impactVfx;
  EcsEntityId          seekEntity;
  GeoVector            seekPos;
};
