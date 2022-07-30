#pragma once
#include "ecs_module.h"
#include "geo_vector.h"

ecs_comp_extern_public(SceneLocomotionComp) {
  f32       speed;
  f32       walkWeight;
  GeoVector target;
};
