#pragma once
#include "ecs_module.h"
#include "geo_vector.h"

typedef enum {
  SceneLocomotion_Arrived = 1 << 0,
} SceneLocomotionFlags;

ecs_comp_extern_public(SceneLocomotionComp) {
  SceneLocomotionFlags flags;
  f32                  speed;
  f32                  walkWeight;
  GeoVector            target;
};

void scene_locomotion_move_to(SceneLocomotionComp*, GeoVector target);
