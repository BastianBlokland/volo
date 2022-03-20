#pragma once
#include "ecs_module.h"
#include "geo_matrix.h"
#include "scene_tag.h"

typedef enum {
  SceneCameraFlags_None         = 0,
  SceneCameraFlags_Vertical     = 1 << 1,
  SceneCameraFlags_Orthographic = 1 << 2,
} SceneCameraFlags;

ecs_comp_extern_public(SceneCameraComp) {
  f32              persFov;
  f32              persNear;
  f32              orthoSize;
  SceneCameraFlags flags;
  SceneTagFilter   filter;
};

ecs_comp_extern_public(SceneCameraMovementComp) {
  f32  moveSpeed;
  bool locked;
};

GeoMatrix scene_camera_proj(const SceneCameraComp*, f32 aspect);

void scene_camera_to_default(SceneCameraComp*);
