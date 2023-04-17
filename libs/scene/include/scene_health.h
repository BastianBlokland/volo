#pragma once
#include "core_time.h"
#include "ecs_entity.h"
#include "ecs_module.h"

typedef enum {
  SceneHealthFlags_None = 0,
  SceneHealthFlags_Dead = 1 << 0,
} SceneHealthFlags;

ecs_comp_extern_public(SceneHealthComp) {
  SceneHealthFlags flags;
  f32              norm;
  f32              max;
  TimeDuration     deathDestroyDelay;
  StringHash       deathEffectPrefab;
};

ecs_comp_extern_public(SceneDamageComp) {
  f32          amount;
  TimeDuration lastDamagedTime;
};

f32 scene_health_points(const SceneHealthComp*);

void scene_health_damage(EcsWorld*, EcsEntityId, f32 amount);
