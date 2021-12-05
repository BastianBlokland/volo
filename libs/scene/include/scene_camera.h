#pragma once
#include "ecs_module.h"
#include "scene_transform.h"

typedef enum {
  SceneCameraFlags_None     = 0,
  SceneCameraFlags_Vertical = 1 << 1,
} SceneCameraFlags;

ecs_comp_extern_public(SceneCameraComp) {
  f32              fov;
  f32              zNear;
  SceneCameraFlags flags;
};

GeoMatrix scene_camera_proj(const SceneCameraComp*, f32 aspect);
GeoMatrix scene_camera_viewproj(const SceneCameraComp*, const SceneTransformComp*, f32 aspect);
