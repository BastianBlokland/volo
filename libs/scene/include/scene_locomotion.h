#pragma once
#include "ecs_module.h"
#include "geo_vector.h"

typedef enum {
  SceneLocomotion_Moving = 1 << 0,
  SceneLocomotion_Stop   = 1 << 1,
} SceneLocomotionFlags;

ecs_comp_extern_public(SceneLocomotionComp) {
  SceneLocomotionFlags flags;
  f32                  maxSpeed, speedNorm;
  f32                  radius;
  GeoVector            lastSeparation;
  GeoVector            targetPos;
  GeoVector            targetDir;
};

void scene_locomotion_move(SceneLocomotionComp*, GeoVector position);
void scene_locomotion_face(SceneLocomotionComp*, GeoVector direction);
void scene_locomotion_stop(SceneLocomotionComp*);
