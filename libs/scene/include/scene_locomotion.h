#pragma once
#include "ecs_module.h"
#include "geo_vector.h"

typedef enum {
  SceneLocomotion_Moving = 1 << 0,
  SceneLocomotion_Stop   = 1 << 1,
} SceneLocomotionFlags;

ecs_comp_extern_public(SceneLocomotionComp) {
  SceneLocomotionFlags flags;
  f32                  maxSpeed;         // Meter per second.
  f32                  rotationSpeedRad; // Radians per second.
  f32                  radius;
  StringHash           moveAnimation; // Optional: 0 to disable.
  GeoVector            lastSeparation;
  GeoVector            targetPos;
  GeoVector            targetDir;
};

void scene_locomotion_move(SceneLocomotionComp*, GeoVector position);
void scene_locomotion_face(SceneLocomotionComp*, GeoVector direction);
void scene_locomotion_stop(SceneLocomotionComp*);
