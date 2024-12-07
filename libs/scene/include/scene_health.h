#pragma once
#include "core_time.h"
#include "ecs_module.h"

typedef enum {
  SceneHealthFlags_None = 0,
  SceneHealthFlags_Dead = 1 << 0,
} SceneHealthFlags;

typedef struct {
  EcsEntityId instigator;
  f32         amount; // Negative for damage, positive for healing.
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

typedef enum {
  SceneHealthStat_DealtDamage,
  SceneHealthStat_DealtHealing,
  SceneHealthStat_Kills,

  SceneHealthStat_Count,
} SceneHealthStat;

ecs_comp_extern_public(SceneHealthStatsComp) { f32 values[SceneHealthStat_Count]; };

ecs_comp_extern_public(SceneDeadComp);

String scene_health_stat_name(SceneHealthStat);
f32    scene_health_points(const SceneHealthComp*);

void scene_health_request_add(SceneHealthRequestComp*, const SceneHealthMod*);
void scene_health_request(EcsWorld*, EcsEntityId target, const SceneHealthMod*);
