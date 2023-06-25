#pragma once
#include "core_time.h"
#include "ecs_entity.h"
#include "ecs_module.h"

typedef enum {
  SceneHealthFlags_None = 0,
  SceneHealthFlags_Dead = 1 << 0,
} SceneHealthFlags;

typedef struct {
  EcsEntityId instigator;
  f32         amount;
} SceneDamageInfo;

ecs_comp_extern_public(SceneHealthComp) {
  SceneHealthFlags flags;
  f32              norm;
  f32              max;
  TimeDuration     lastDamagedTime;
  TimeDuration     deathDestroyDelay;
  StringHash       deathEffectPrefab;
};

typedef struct {
  SceneDamageInfo* values;
  u32              count, capacity;
} SceneDamageStorage;

ecs_comp_extern_public(SceneDamageComp) {
  union {
    SceneDamageInfo    request;
    SceneDamageStorage storage;
  };
  bool singleRequest;
};

ecs_comp_extern_public(SceneDeadComp);

f32 scene_health_points(const SceneHealthComp*);

void scene_health_damage(EcsWorld*, EcsEntityId target, const SceneDamageInfo*);
