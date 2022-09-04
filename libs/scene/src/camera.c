#include "core_diag.h"
#include "core_math.h"
#include "ecs_world.h"
#include "scene_camera.h"
#include "scene_transform.h"

static const f32 g_camOrthoNear = -100.0f;
static const f32 g_camOrthoFar  = +100.0f;

static GeoVector cam_world_from_ndc(const GeoMatrix* invViewProj, const GeoVector pos) {
  const GeoVector ndc = geo_vector(pos.x, pos.y, pos.z, 1);
  return geo_vector_perspective_div(geo_matrix_transform(invViewProj, ndc));
}

static GeoVector cam_world_from_screen_near(const GeoMatrix* invViewProj, const GeoVector normPos) {
  const f32 ndcX    = normPos.x * 2 - 1;
  const f32 ndcY    = -normPos.y * 2 + 1;
  const f32 ndcNear = 1.0f;
  return cam_world_from_ndc(invViewProj, geo_vector(ndcX, ndcY, ndcNear));
}

static GeoVector cam_world_from_screen_far(const GeoMatrix* invViewProj, const GeoVector normPos) {
  const f32 ndcX   = normPos.x * 2 - 1;
  const f32 ndcY   = -normPos.y * 2 + 1;
  const f32 ndcFar = 1e-4f; // NOTE: Using an infinitely far depth plane so avoid 0.
  return cam_world_from_ndc(invViewProj, geo_vector(ndcX, ndcY, ndcFar));
}

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

void scene_camera_frustum4(
    const SceneCameraComp*    cam,
    const SceneTransformComp* trans,
    const f32                 aspect,
    GeoPlane                  out[4]) {
  const GeoMatrix viewProj = scene_camera_view_proj(cam, trans, aspect);
  geo_matrix_frustum4(&viewProj, out);
}

void scene_camera_frustum_corners(
    const SceneCameraComp*    cam,
    const SceneTransformComp* trans,
    const f32                 aspect,
    const GeoVector           rectMin,
    const GeoVector           rectMax,
    GeoVector                 out[8]) {
  diag_assert(rectMin.x < rectMax.x && rectMin.y < rectMax.y);

  const GeoMatrix viewProj    = scene_camera_view_proj(cam, trans, aspect);
  const GeoMatrix invViewProj = geo_matrix_inverse(&viewProj);

  out[0] = cam_world_from_screen_near(&invViewProj, geo_vector(rectMin.x, rectMin.y));
  out[1] = cam_world_from_screen_near(&invViewProj, geo_vector(rectMin.x, rectMax.y));
  out[2] = cam_world_from_screen_near(&invViewProj, geo_vector(rectMax.x, rectMax.y));
  out[3] = cam_world_from_screen_near(&invViewProj, geo_vector(rectMax.x, rectMin.y));
  out[4] = cam_world_from_screen_far(&invViewProj, geo_vector(rectMin.x, rectMin.y));
  out[5] = cam_world_from_screen_far(&invViewProj, geo_vector(rectMin.x, rectMax.y));
  out[6] = cam_world_from_screen_far(&invViewProj, geo_vector(rectMax.x, rectMax.y));
  out[7] = cam_world_from_screen_far(&invViewProj, geo_vector(rectMax.x, rectMin.y));
}

GeoRay scene_camera_ray(
    const SceneCameraComp*    cam,
    const SceneTransformComp* trans,
    const f32                 aspect,
    const GeoVector           normScreenPos) {
  const GeoMatrix viewProj    = scene_camera_view_proj(cam, trans, aspect);
  const GeoMatrix invViewProj = geo_matrix_inverse(&viewProj);

  const GeoVector posNear = cam_world_from_screen_near(&invViewProj, normScreenPos);
  const GeoVector posFar  = cam_world_from_screen_far(&invViewProj, normScreenPos);
  return (GeoRay){.point = posNear, .dir = geo_vector_norm(geo_vector_sub(posFar, posNear))};
}

void scene_camera_to_default(SceneCameraComp* cam) {
  cam->persFov   = 60.0f * math_deg_to_rad;
  cam->orthoSize = 5.0f;
  cam->persNear  = 0.1f;
  cam->flags &= ~SceneCameraFlags_Vertical;
}
