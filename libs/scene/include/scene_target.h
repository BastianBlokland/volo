#pragma once
#include "core_time.h"
#include "ecs_entity.h"
#include "ecs_module.h"
#include "geo_vector.h"

typedef enum {
  SceneTarget_LineOfSight          = 1 << 0,
  SceneTarget_InstantRefreshOnIdle = 1 << 1, // Immediately refresh when current target is gone.
} SceneTargetFlags;

ecs_comp_extern_public(SceneTargetFinderComp) {
  EcsEntityId      target;
  EcsEntityId      targetOverride;
  SceneTargetFlags targetFlags;
  f32              targetScoreSqr;
  f32              lineOfSightRadius;
  f32              scoreRandomness; // Maximum target score to add randomly.
  TimeDuration     nextRefreshTime;
  GeoVector        targetPosition;
};
