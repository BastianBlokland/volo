#pragma once
#include "ecs_module.h"
#include "geo_matrix.h"
#include "scene_tag.h"

// Forward declare from 'scene_transform.h'.
ecs_comp_extern(SceneTransformComp);

typedef enum {
  SceneCameraFlags_None         = 0,
  SceneCameraFlags_Vertical     = 1 << 1,
  SceneCameraFlags_Orthographic = 1 << 2,
  SceneCameraFlags_DebugFrustum = 1 << 3, // Visualize the frustum for debugging purposes.
} SceneCameraFlags;

ecs_comp_extern_public(SceneCameraComp) {
  f32              persFov;
  f32              persNear;
  f32              orthoSize;
  SceneCameraFlags flags;
  SceneTagFilter   filter;
};

ecs_comp_extern_public(SceneCameraMovementComp) { f32 moveSpeed; };

/**
 * Compute the projection matrix at the given aspect.
 */
GeoMatrix scene_camera_proj(const SceneCameraComp*, f32 aspect);

/**
 * Compute the view-projection matrix at the given aspect.
 * NOTE: SceneTransformComp is optional.
 */
GeoMatrix scene_camera_view_proj(const SceneCameraComp*, const SceneTransformComp*, f32 aspect);

void scene_camera_to_default(SceneCameraComp*);
void scene_camera_movement_to_default(SceneCameraMovementComp*);
