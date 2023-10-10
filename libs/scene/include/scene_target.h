#pragma once
#include "core_time.h"
#include "ecs_entity.h"
#include "ecs_module.h"
#include "geo_vector.h"

#define scene_target_queue_size 4

typedef enum {
  SceneTarget_ConfigExcludeUnreachable = 1 << 0,
  SceneTarget_ConfigExcludeObscured    = 1 << 1,
  SceneTarget_ConfigTrace              = 1 << 2, // Enable diagnostic tracing.
  SceneTarget_LineOfSight              = 1 << 3, // Set while we have los to target.
  SceneTarget_Overriden                = 1 << 4, // Set while we use an overriden target.
} SceneTargetFlags;

ecs_comp_extern_public(SceneTargetFinderComp) {
  SceneTargetFlags flags;
  f32              rangeMin, rangeMax;
  f32              lineOfSightRadius;
  f32              targetDistance;
  TimeDuration     nextRefreshTime;
  EcsEntityId      targetOverride;
  EcsEntityId      targetQueue[scene_target_queue_size];
  GeoVector        targetPosition;
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
