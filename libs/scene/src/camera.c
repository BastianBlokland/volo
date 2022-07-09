#include "core_math.h"
#include "ecs_world.h"
#include "gap_window.h"
#include "input.h"
#include "scene_camera.h"
#include "scene_time.h"
#include "scene_transform.h"

static const f32       g_camMoveSpeed          = 10.0f;
static const f32       g_camMoveSpeedBoostMult = 4.0f;
static const f32       g_camRotateSensitivity  = 0.0025f;
static const f32       g_camPersFov            = 60.0f * math_deg_to_rad;
static const f32       g_camPersNear           = 0.1f;
static const GeoVector g_camPersPosition       = {0, 1.7f, -3.0f};
static const f32       g_camPersAngle          = 10 * math_deg_to_rad;
static const f32       g_camOrthoSize          = 5.0f;
static const f32       g_camOrthoNear          = -1e4f;
static const f32       g_camOrthoFar           = +1e4f;

ecs_comp_define_public(SceneCameraComp);
ecs_comp_define_public(SceneCameraMovementComp);

ecs_view_define(GlobalView) {
  ecs_access_write(InputManagerComp);
  ecs_access_read(SceneTimeComp);
}

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

    ecs_world_add_t(world, entity, SceneCameraMovementComp, .moveSpeed = g_camMoveSpeed);

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

static void camera_update_move(
    const SceneCameraComp*         cam,
    const SceneCameraMovementComp* move,
    SceneTransformComp*            trans,
    const InputManagerComp*        input,
    const f32                      deltaSeconds) {
  const bool boosted   = input_triggered_lit(input, "CameraMoveBoost");
  const f32  moveSpeed = move->moveSpeed * (boosted ? g_camMoveSpeedBoostMult : 1.0f);
  const f32  posDelta  = deltaSeconds * moveSpeed;

  const GeoVector right   = geo_quat_rotate(trans->rotation, geo_right);
  const GeoVector up      = geo_quat_rotate(trans->rotation, geo_up);
  const GeoVector forward = geo_quat_rotate(trans->rotation, geo_forward);

  if (input_triggered_lit(input, "CameraMoveForward")) {
    const GeoVector dir = cam->flags & SceneCameraFlags_Orthographic ? up : forward;
    trans->position     = geo_vector_add(trans->position, geo_vector_mul(dir, posDelta));
  }
  if (input_triggered_lit(input, "CameraMoveBackward")) {
    const GeoVector dir = cam->flags & SceneCameraFlags_Orthographic ? up : forward;
    trans->position     = geo_vector_sub(trans->position, geo_vector_mul(dir, posDelta));
  }
  if (input_triggered_lit(input, "CameraMoveRight")) {
    trans->position = geo_vector_add(trans->position, geo_vector_mul(right, posDelta));
  }
  if (input_triggered_lit(input, "CameraMoveLeft")) {
    trans->position = geo_vector_sub(trans->position, geo_vector_mul(right, posDelta));
  }
}

static void camera_update_rotate(SceneTransformComp* trans, const InputManagerComp* input) {
  const GeoVector left         = geo_quat_rotate(trans->rotation, geo_left);
  const bool      cursorLocked = input_cursor_mode(input) == InputCursorMode_Locked;
  const bool      lookEnable   = input_triggered_lit(input, "CameraLookEnable") || cursorLocked;

  if (lookEnable) {
    const f32 deltaX = input_cursor_delta_x(input) * g_camRotateSensitivity;
    const f32 deltaY = input_cursor_delta_y(input) * g_camRotateSensitivity;

    trans->rotation = geo_quat_mul(geo_quat_angle_axis(left, deltaY), trans->rotation);
    trans->rotation = geo_quat_mul(geo_quat_angle_axis(geo_up, deltaX), trans->rotation);
    trans->rotation = geo_quat_norm(trans->rotation);
  }
}

ecs_view_define(CameraUpdateView) {
  ecs_access_write(SceneCameraComp);
  ecs_access_write(SceneCameraMovementComp);
  ecs_access_write(SceneTransformComp);
}

ecs_system_define(SceneCameraUpdateSys) {
  EcsView*     view      = ecs_world_view_t(world, GlobalView);
  EcsIterator* globalItr = ecs_view_maybe_at(view, ecs_world_global(world));
  if (!globalItr) {
    return;
  }
  InputManagerComp*    input        = ecs_view_write_t(globalItr, InputManagerComp);
  const SceneTimeComp* time         = ecs_view_read_t(globalItr, SceneTimeComp);
  const f32            deltaSeconds = time->delta / (f32)time_second;

  EcsView* cameraView = ecs_world_view_t(world, CameraUpdateView);
  for (EcsIterator* itr = ecs_view_itr(cameraView); ecs_view_walk(itr);) {
    SceneCameraComp*         cam   = ecs_view_write_t(itr, SceneCameraComp);
    SceneTransformComp*      trans = ecs_view_write_t(itr, SceneTransformComp);
    SceneCameraMovementComp* move  = ecs_view_write_t(itr, SceneCameraMovementComp);

    if (input_active_window(input) != ecs_view_entity(itr)) {
      continue; // Ignore input for camera's where the window is not active.
    }
    camera_update_move(cam, move, trans, input, deltaSeconds);
    camera_update_rotate(trans, input);
  }
}

ecs_module_init(scene_camera_module) {
  ecs_register_comp(SceneCameraComp);
  ecs_register_comp(SceneCameraMovementComp);

  ecs_register_view(GlobalView);
  ecs_register_view(CameraCreateView);
  ecs_register_view(CameraUpdateView);

  ecs_register_system(SceneCameraCreateSys, ecs_view_id(CameraCreateView));

  ecs_register_system(SceneCameraUpdateSys, ecs_view_id(GlobalView), ecs_view_id(CameraUpdateView));
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

  return (GeoRay){.point = orig, .direction = geo_vector_norm(geo_vector_sub(dest, orig))};
}

void scene_camera_to_default(SceneCameraComp* cam) {
  cam->persFov   = g_camPersFov;
  cam->orthoSize = g_camOrthoSize;
  cam->persNear  = g_camPersNear;
  cam->flags &= ~SceneCameraFlags_Vertical;
}

void scene_camera_movement_to_default(SceneCameraMovementComp* camMovement) {
  camMovement->moveSpeed = g_camMoveSpeed;
}
