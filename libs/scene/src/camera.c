#include "geo_matrix.h"
#include "scene_camera.h"

ecs_comp_define_public(SceneCameraComp);

GeoMatrix scene_camera_proj(const SceneCameraComp* cam, const f32 aspect) {
  if (cam->flags & SceneCameraFlags_Vertical) {
    return geo_matrix_proj_pers_ver(cam->fov, aspect, cam->zNear);
  }
  return geo_matrix_proj_pers_hor(cam->fov, aspect, cam->zNear);
}

GeoMatrix scene_camera_viewproj(
    const SceneCameraComp* cam, const SceneTransformComp* trans, const f32 aspect) {
  const GeoMatrix proj = scene_camera_proj(cam, aspect);
  const GeoMatrix view = scene_transform_matrix_inv(trans);
  return geo_matrix_mul(&proj, &view);
}

ecs_module_init(scene_camera_module) { ecs_register_comp(SceneCameraComp); }
