#pragma once
#include "ecs_entity.h"
#include "ecs_module.h"
#include "geo_vector.h"

ecs_comp_extern_public(SceneTargetFinderComp) {
  EcsEntityId target;
  f32         targetDistSqr;
  GeoVector   targetPosition;
};
