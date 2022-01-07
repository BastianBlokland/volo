#include "ecs_world.h"
#include "gap_window.h"
#include "log.h"
#include "scene_camera.h"
#include "scene_time.h"
#include "scene_transform.h"

ecs_comp_define_public(SceneCameraComp);
ecs_comp_define_public(SceneCameraMovementComp);
ecs_comp_define(SceneCameraInternalComp) { GapVector lastWindowedSize; };

ecs_view_define(GlobalView) { ecs_access_read(SceneTimeComp); }

ecs_view_define(CameraMovementView) {
  ecs_access_with(SceneCameraComp);
  ecs_access_write(SceneTransformComp);
  ecs_access_write(GapWindowComp);

  ecs_access_write(SceneCameraMovementComp);
  ecs_access_maybe_write(SceneCameraInternalComp);
}

ecs_system_define(SceneCameraMovementSys) {
  EcsView*     view      = ecs_world_view_t(world, GlobalView);
  EcsIterator* globalItr = ecs_view_maybe_at(view, ecs_world_global(world));
  if (!globalItr) {
    return;
  }
  const SceneTimeComp* time = ecs_view_read_t(globalItr, SceneTimeComp);

  static const f32 rotSensitivity = 0.0025f;
  const f32        deltaSeconds   = time->delta / (f32)time_second;

  EcsView* cameraView = ecs_world_view_t(world, CameraMovementView);
  for (EcsIterator* itr = ecs_view_itr(cameraView); ecs_view_walk(itr);) {
    GapWindowComp*           win   = ecs_view_write_t(itr, GapWindowComp);
    SceneTransformComp*      trans = ecs_view_write_t(itr, SceneTransformComp);
    SceneCameraMovementComp* move  = ecs_view_write_t(itr, SceneCameraMovementComp);

    SceneCameraInternalComp* internal = ecs_view_write_t(itr, SceneCameraInternalComp);
    if (!internal) {
      internal = ecs_world_add_t(world, ecs_view_entity(itr), SceneCameraInternalComp);
    }

    const f32 posDelta = deltaSeconds * move->moveSpeed;

    const GeoVector forward = geo_quat_rotate(trans->rotation, geo_forward);
    const GeoVector right   = geo_quat_rotate(trans->rotation, geo_right);

    if (gap_window_key_down(win, GapKey_W) || gap_window_key_down(win, GapKey_ArrowUp)) {
      trans->position = geo_vector_add(trans->position, geo_vector_mul(forward, posDelta));
    }
    if (gap_window_key_down(win, GapKey_S) || gap_window_key_down(win, GapKey_ArrowDown)) {
      trans->position = geo_vector_sub(trans->position, geo_vector_mul(forward, posDelta));
    }
    if (gap_window_key_down(win, GapKey_D) || gap_window_key_down(win, GapKey_ArrowRight)) {
      trans->position = geo_vector_add(trans->position, geo_vector_mul(right, posDelta));
    }
    if (gap_window_key_down(win, GapKey_A) || gap_window_key_down(win, GapKey_ArrowLeft)) {
      trans->position = geo_vector_sub(trans->position, geo_vector_mul(right, posDelta));
    }

    if (gap_window_key_pressed(win, GapKey_Tab)) {
      if (move->locked) {
        gap_window_flags_unset(win, GapWindowFlags_CursorLock | GapWindowFlags_CursorHide);
      } else {
        gap_window_flags_set(win, GapWindowFlags_CursorLock | GapWindowFlags_CursorHide);
      }
      log_i("Update camera lock", log_param("locked", fmt_bool(move->locked)));
      move->locked ^= true;
    }

    if (gap_window_key_pressed(win, GapKey_F)) {
      if (gap_window_mode(win) == GapWindowMode_Fullscreen) {
        gap_window_resize(win, internal->lastWindowedSize, GapWindowMode_Windowed);
      } else {
        internal->lastWindowedSize = gap_window_param(win, GapParam_WindowSize);
        gap_window_resize(win, gap_vector(0, 0), GapWindowMode_Fullscreen);
      }
    }

    const bool lookEnable = gap_window_key_down(win, GapKey_MouseRight) ||
                            gap_window_key_down(win, GapKey_Control) || move->locked;
    if (lookEnable) {
      const f32 deltaX = gap_window_param(win, GapParam_CursorDelta).x * rotSensitivity;
      const f32 deltaY = gap_window_param(win, GapParam_CursorDelta).y * rotSensitivity;

      trans->rotation = geo_quat_mul(geo_quat_angle_axis(right, deltaY), trans->rotation);
      trans->rotation = geo_quat_mul(geo_quat_angle_axis(geo_up, deltaX), trans->rotation);
      trans->rotation = geo_quat_norm(trans->rotation);
    }
  }
}

ecs_module_init(scene_camera_module) {
  ecs_register_comp(SceneCameraComp);
  ecs_register_comp(SceneCameraMovementComp);
  ecs_register_comp(SceneCameraInternalComp);

  ecs_register_view(GlobalView);
  ecs_register_view(CameraMovementView);

  ecs_register_system(
      SceneCameraMovementSys, ecs_view_id(GlobalView), ecs_view_id(CameraMovementView));
}

GeoMatrix scene_camera_proj(const SceneCameraComp* cam, const f32 aspect) {
  if (cam->flags & SceneCameraFlags_Vertical) {
    return geo_matrix_proj_pers_ver(cam->fov, aspect, cam->zNear);
  }
  return geo_matrix_proj_pers_hor(cam->fov, aspect, cam->zNear);
}
