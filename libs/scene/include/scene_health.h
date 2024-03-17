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
} SceneHealthMod;

ecs_comp_extern_public(SceneHealthComp) {
  SceneHealthFlags flags;
  f32              norm;
  f32              max;
  TimeDuration     lastDamagedTime;
  TimeDuration     deathDestroyDelay;
  StringHash       deathEffectPrefab;
};

typedef struct {
  SceneHealthMod* values;
  u32             count, capacity;
} SceneHealthModStorage;

ecs_comp_extern_public(SceneHealthRequestComp) {
  bool singleRequest;
  union {
    SceneHealthMod        request;
    SceneHealthModStorage storage;
  };
};

ecs_comp_extern_public(SceneHealthStatsComp) {
  f32 dealtDamage;
  u32 kills;
};

ecs_comp_extern_public(SceneDeadComp);

f32 scene_health_points(const SceneHealthComp*);

void scene_health_request_add(SceneHealthRequestComp*, const SceneHealthMod*);
void scene_health_request(EcsWorld*, EcsEntityId target, const SceneHealthMod*);
