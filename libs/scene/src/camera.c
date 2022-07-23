#include "core_math.h"
#include "ecs_world.h"
#include "gap_window.h"
#include "scene_camera.h"
#include "scene_transform.h"

static const f32       g_camPersFov      = 60.0f * math_deg_to_rad;
static const f32       g_camPersNear     = 0.1f;
static const GeoVector g_camPersPosition = {0, 1.2f, -3.5f};
static const f32       g_camPersAngle    = 5 * math_deg_to_rad;
static const f32       g_camOrthoSize    = 5.0f;
static const f32       g_camOrthoNear    = -100.0f;
static const f32       g_camOrthoFar     = +100.0f;

ecs_comp_define_public(SceneCameraComp);
ecs_comp_define_public(SceneCameraMovementComp);

ecs_view_define(CameraCreateView) {
  ecs_access_with(GapWindowComp);
  ecs_access_without(SceneCameraComp);
}

ecs_system_define(SceneCameraCreateSys) {
  EcsView* createView = ecs_world_view_t(world, CameraCreateView);
  for (EcsIterator* itr = ecs_view_itr(createView); ecs_view_walk(itr);) {
    const EcsEntityId entity = ecs_view_entity(itr);

    ecs_world_add_t(
        world,
        entity,
        SceneCameraComp,
        .persFov   = g_camPersFov,
        .persNear  = g_camPersNear,
        .orthoSize = g_camOrthoSize);

    if (!ecs_world_has_t(world, entity, SceneTransformComp)) {
      ecs_world_add_t(
          world,
          entity,
          SceneTransformComp,
          .position = g_camPersPosition,
          .rotation = geo_quat_angle_axis(geo_right, g_camPersAngle));
    }
  }
}

ecs_module_init(scene_camera_module) {
  ecs_register_comp(SceneCameraComp);

  ecs_register_view(CameraCreateView);

  ecs_register_system(SceneCameraCreateSys, ecs_view_id(CameraCreateView));
}

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
  cam->persFov   = g_camPersFov;
  cam->orthoSize = g_camOrthoSize;
  cam->persNear  = g_camPersNear;
  cam->flags &= ~SceneCameraFlags_Vertical;
}
