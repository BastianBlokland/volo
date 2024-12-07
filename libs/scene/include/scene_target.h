#pragma once
#include "core_time.h"
#include "ecs_module.h"

#define scene_target_queue_size 4

typedef enum {
  SceneTargetConfig_ExcludeUnreachable = 1 << 0,
  SceneTargetConfig_ExcludeObscured    = 1 << 1,
  SceneTargetConfig_Trace              = 1 << 2, // Enable diagnostic tracing.
} SceneTargetConfig;

ecs_comp_extern_public(SceneTargetFinderComp) {
  SceneTargetConfig config;
  f32               rangeMin, rangeMax;
  TimeDuration      nextRefreshTime;
  EcsEntityId       targetQueue[scene_target_queue_size];
};

EcsEntityId scene_target_primary(const SceneTargetFinderComp*);
bool        scene_target_contains(const SceneTargetFinderComp*, EcsEntityId);

typedef struct {
  EcsEntityId entity;
  f32         value;
} SceneTargetScore;

ecs_comp_extern(SceneTargetTraceComp);

const SceneTargetScore* scene_target_trace_begin(const SceneTargetTraceComp*);
const SceneTargetScore* scene_target_trace_end(const SceneTargetTraceComp*);
