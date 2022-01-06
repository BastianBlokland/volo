#pragma once
#include "ecs_module.h"
#include "geo_matrix.h"

typedef enum {
  SceneCameraFlags_None     = 0,
  SceneCameraFlags_Vertical = 1 << 1,
} SceneCameraFlags;

ecs_comp_extern_public(SceneCameraComp) {
  f32              fov;
  f32              zNear;
  SceneCameraFlags flags;
};

ecs_comp_extern_public(SceneCameraMovementComp) {
  f32  moveSpeed;
  bool locked;
};

GeoMatrix scene_camera_proj(const SceneCameraComp*, f32 aspect);
