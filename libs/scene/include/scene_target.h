#pragma once
#include "core_time.h"
#include "ecs_entity.h"
#include "ecs_module.h"
#include "geo_vector.h"

typedef enum {
  SceneTarget_LineOfSight = 1 << 0,
} SceneTargetFlags;

ecs_comp_extern_public(SceneTargetFinderComp) {
  EcsEntityId      target;
  EcsEntityId      targetOverride;
  SceneTargetFlags targetFlags;
  f32              targetScoreSqr;
  TimeDuration     nextRefreshTime;
  GeoVector        targetPosition;
};
