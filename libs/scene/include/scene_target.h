#pragma once
#include "core_time.h"
#include "ecs_entity.h"
#include "ecs_module.h"
#include "geo_vector.h"

typedef enum {
  SceneTarget_ConfigExcludeUnreachable   = 1 << 0,
  SceneTarget_ConfigInstantRefreshOnIdle = 1 << 1, // Instant refresh when target is gone.
  SceneTarget_ConfigTrace                = 1 << 2, // Enable diagnostic tracing.
  SceneTarget_LineOfSight                = 1 << 3, // Set while we have los to target.
  SceneTarget_Overriden                  = 1 << 4, // Set while we use an overriden target.
} SceneTargetFlags;

ecs_comp_extern_public(SceneTargetFinderComp) {
  EcsEntityId      target;
  EcsEntityId      targetOverride;
  SceneTargetFlags flags;
  f32              distanceMax;
  f32              lineOfSightRadius;
  f32              scoreRandom; // Maximum target score to add randomly.
  f32              targetScore;
  f32              targetDistance;
  TimeDuration     nextRefreshTime;
  GeoVector        targetPosition;
};

typedef struct {
  EcsEntityId entity;
  f32         value;
} SceneTargetScore;

ecs_comp_extern(SceneTargetTraceComp);

const SceneTargetScore* scene_target_trace_begin(const SceneTargetTraceComp*);
const SceneTargetScore* scene_target_trace_end(const SceneTargetTraceComp*);
