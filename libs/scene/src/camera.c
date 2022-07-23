#include "core_math.h"
#include "ecs_world.h"
#include "scene_camera.h"
#include "scene_transform.h"

static const f32 g_camOrthoNear = -100.0f;
static const f32 g_camOrthoFar  = +100.0f;

ecs_comp_define_public(SceneCameraComp);

ecs_module_init(scene_camera_module) { ecs_register_comp(SceneCameraComp); }

GeoMatrix scene_camera_proj(const SceneCameraComp* cam, const f32 aspect) {
  if (cam->flags & SceneCameraFlags_Orthographic) {
    if (cam->flags & SceneCameraFlags_Vertical) {
      return geo_matrix_proj_ortho_ver(cam->orthoSize, aspect, g_camOrthoNear, g_camOrthoFar);
    }
    return geo_matrix_proj_ortho_hor(cam->orthoSize, aspect, g_camOrthoNear, g_camOrthoFar);
  }

  if (cam->flags & SceneCameraFlags_Vertical) {
    return geo_matrix_proj_pers_ver(cam->persFov, aspect, cam->persNear);
  }
  return geo_matrix_proj_pers_hor(cam->persFov, aspect, cam->persNear);
}

GeoMatrix scene_camera_view_proj(
    const SceneCameraComp* cam, const SceneTransformComp* trans, const f32 aspect) {

  const GeoMatrix p = scene_camera_proj(cam, aspect);
  const GeoMatrix v = LIKELY(trans) ? scene_transform_matrix_inv(trans) : geo_matrix_ident();
  return geo_matrix_mul(&p, &v);
}

GeoRay scene_camera_ray(
    const SceneCameraComp*    cam,
    const SceneTransformComp* trans,
    const f32                 aspect,
    const GeoVector           normScreenPos) {
  const GeoMatrix viewProj    = scene_camera_view_proj(cam, trans, aspect);
  const GeoMatrix invViewProj = geo_matrix_inverse(&viewProj);
  const f32       ndcX        = normScreenPos.x * 2 - 1;
  const f32       ndcY        = -normScreenPos.y * 2 + 1;
  const f32       ndcNear     = 1.0f;
  const f32       ndcFar      = 1e-8f; // NOTE: Using an infinitely far depth plane so avoid 0.

  const GeoVector origNdc = geo_vector(ndcX, ndcY, ndcNear, 1);
  const GeoVector orig    = geo_vector_perspective_div(geo_matrix_transform(&invViewProj, origNdc));

  const GeoVector destNdc = geo_vector(ndcX, ndcY, ndcFar, 1);
  const GeoVector dest    = geo_vector_perspective_div(geo_matrix_transform(&invViewProj, destNdc));

  return (GeoRay){.point = orig, .dir = geo_vector_norm(geo_vector_sub(dest, orig))};
}

void scene_camera_to_default(SceneCameraComp* cam) {
  cam->persFov   = 60.0f * math_deg_to_rad;
  cam->orthoSize = 5.0f;
  cam->persNear  = 0.1f;
  cam->flags &= ~SceneCameraFlags_Vertical;
}
