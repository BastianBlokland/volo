#pragma once
#include "core_time.h"
#include "ecs_entity.h"
#include "ecs_module.h"
#include "geo_vector.h"

typedef enum {
  SceneTarget_LineOfSight          = 1 << 0,
  SceneTarget_Overriden            = 1 << 1,
  SceneTarget_ExcludeUnreachable   = 1 << 2,
  SceneTarget_InstantRefreshOnIdle = 1 << 3, // Immediately refresh when current target is gone.
  SceneTarget_Trace                = 1 << 4, // Enable diagnostic tracing.
} SceneTargetFlags;

ecs_comp_extern_public(SceneTargetFinderComp) {
  EcsEntityId      target;
  EcsEntityId      targetOverride;
  SceneTargetFlags flags;
  f32              targetScore;
  f32              targetDistance;
  f32              lineOfSightRadius;
  f32              scoreRandomness; // Maximum target score to add randomly.
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
