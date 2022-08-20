#pragma once
#include "ecs_module.h"
#include "geo_matrix.h"
#include "geo_ray.h"
#include "scene_tag.h"

// Forward declare from 'scene_transform.h'.
ecs_comp_extern(SceneTransformComp);

typedef enum {
  SceneCameraFlags_None                  = 0,
  SceneCameraFlags_Vertical              = 1 << 1,
  SceneCameraFlags_Orthographic          = 1 << 2,
  SceneCameraFlags_DebugGizmoTranslation = 1 << 3, // Enable debug translation gizmo.
  SceneCameraFlags_DebugGizmoRotation    = 1 << 4, // Enable debug rotation gizmo.
  SceneCameraFlags_DebugFrustum          = 1 << 5, // Visualize the frustum.
  SceneCameraFlags_DebugInputRay         = 1 << 6, // Visualize the input ray.
} SceneCameraFlags;

ecs_comp_extern_public(SceneCameraComp) {
  f32              persFov;
  f32              persNear;
  f32              orthoSize;
  SceneCameraFlags flags;
  SceneTagFilter   filter;
};

/**
 * Compute the projection matrix at the given aspect.
 */
GeoMatrix scene_camera_proj(const SceneCameraComp*, f32 aspect);

/**
 * Compute the view-projection matrix at the given aspect.
 * NOTE: SceneTransformComp is optional.
 */
GeoMatrix scene_camera_view_proj(const SceneCameraComp*, const SceneTransformComp*, f32 aspect);

/**
 * Compute 4 frustum planes.
 * NOTE: Plane normals point towards the inside of the frustum.
 * NOTE: SceneTransformComp is optional
 *
 * [0] = Left plane.
 * [1] = Right plane.
 * [2] = Top plane.
 * [3] = Bottom plane.
 */
void scene_camera_frustum4(
    const SceneCameraComp*, const SceneTransformComp*, f32 aspect, GeoPlane out[4]);

/**
 * Compute 4 frustum planes for a rectangle inside the camera view.
 * NOTE: Plane normals point towards the inside of the frustum.
 * NOTE: SceneTransformComp is optional
 * NOTE: Rect coordinates are in normalized screen positions (x: 0 - 1, y: 0 - 1).
 *
 * [0] = Left plane.
 * [1] = Right plane.
 * [2] = Top plane.
 * [3] = Bottom plane.
 *
 * Pre-condition: Given rectangle is not inverted.
 * Pre-condition: Given rectangle is not infinitely small.
 */
void scene_camera_frustum4_rect(
    const SceneCameraComp*,
    const SceneTransformComp*,
    f32       aspect,
    GeoVector rectMin,
    GeoVector rectMax,
    GeoPlane  out[4]);

/**
 * Compute a world-space ray through the given normalized screen position (x: 0 - 1, y: 0 - 1).
 */
GeoRay scene_camera_ray(
    const SceneCameraComp*, const SceneTransformComp*, f32 aspect, GeoVector normScreenPos);

void scene_camera_to_default(SceneCameraComp*);
