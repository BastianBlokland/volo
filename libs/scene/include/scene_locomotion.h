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
  f32                  radius, weight;
  f32                  moveFaceThreshold;    // 1.0 = exact facing, 0.0 = not facing at all.
  StringHash           moveAnimation;        // Optional: 0 to disable.
  f32                  lastSeparationMagSqr; // Squared magnitude of last frame's separation delta.
  GeoVector            targetPos;
  GeoVector            targetDir;
};

ecs_comp_extern_public(SceneLocomotionAlignComp) { GeoVector terrainNormal; };

f32 scene_locomotion_radius(const SceneLocomotionComp*, f32 scale);
f32 scene_locomotion_weight(const SceneLocomotionComp*, f32 scale);

void scene_locomotion_move(SceneLocomotionComp*, GeoVector position);
void scene_locomotion_face(SceneLocomotionComp*, GeoVector direction);
void scene_locomotion_stop(SceneLocomotionComp*);
