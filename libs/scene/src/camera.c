#include "asset_manager.h"
#include "core_math.h"
#include "ecs_utils.h"
#include "ecs_world.h"
#include "gap_window.h"
#include "log.h"
#include "scene_camera.h"
#include "scene_renderable.h"
#include "scene_time.h"
#include "scene_transform.h"

static const f32       g_cameraDefaultFov         = 60.0f * math_deg_to_rad;
static const f32       g_cameraDefaultNearPlane   = 0.1f;
static const GeoVector g_cameraDefaultPosition    = {0, 1.5f, -3.0f};
static const f32       g_cameraDefaultAngle       = 10 * math_deg_to_rad;
static const f32       g_cameraDefaultMoveSpeed   = 10.0f;
static const f32       g_cameraRotateSensitivity  = 0.0025f;
static const f32       g_cameraBoostMoveSpeedMult = 4.0f;

ecs_comp_define_public(SceneCameraComp);
ecs_comp_define_public(SceneCameraMovementComp);

ecs_comp_define(SceneCameraInternalComp) { GapVector lastWindowedSize; };
ecs_comp_define(SceneCameraSkyComp);

ecs_view_define(GlobalTimeView) { ecs_access_read(SceneTimeComp); }
ecs_view_define(GlobalAssetsView) { ecs_access_write(AssetManagerComp); }
ecs_view_define(SkyView) { ecs_access_with(SceneCameraSkyComp); }

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
        .fov   = g_cameraDefaultFov,
        .zNear = g_cameraDefaultNearPlane);

    ecs_world_add_t(world, entity, SceneCameraMovementComp, .moveSpeed = g_cameraDefaultMoveSpeed);

    if (!ecs_world_has_t(world, entity, SceneTransformComp)) {
      ecs_world_add_t(
          world,
          entity,
          SceneTransformComp,
          .position = g_cameraDefaultPosition,
          .rotation = geo_quat_angle_axis(geo_right, g_cameraDefaultAngle));
    }
  }
}

ecs_system_define(SceneCameraCreateSkySys) {
  if (ecs_utils_any(world, SkyView)) {
    return;
  }

  EcsView*     view      = ecs_world_view_t(world, GlobalAssetsView);
  EcsIterator* globalItr = ecs_view_maybe_at(view, ecs_world_global(world));
  if (!globalItr) {
    return;
  }
  AssetManagerComp* assets = ecs_view_write_t(globalItr, AssetManagerComp);

  const EcsEntityId skyEntity = ecs_world_entity_create(world);
  ecs_world_add_empty_t(world, skyEntity, SceneCameraSkyComp);
  ecs_world_add_t(
      world,
      skyEntity,
      SceneRenderableUniqueComp,
      .graphic = asset_lookup(world, assets, string_lit("graphics/sky.gra")));
}

ecs_view_define(CameraMovementView) {
  ecs_access_with(SceneCameraComp);
  ecs_access_write(SceneTransformComp);
  ecs_access_write(GapWindowComp);

  ecs_access_write(SceneCameraMovementComp);
  ecs_access_maybe_write(SceneCameraInternalComp);
}

ecs_system_define(SceneCameraMovementSys) {
  EcsView*     view      = ecs_world_view_t(world, GlobalTimeView);
  EcsIterator* globalItr = ecs_view_maybe_at(view, ecs_world_global(world));
  if (!globalItr) {
    return;
  }
  const SceneTimeComp* time         = ecs_view_read_t(globalItr, SceneTimeComp);
  const f32            deltaSeconds = time->delta / (f32)time_second;

  EcsView* cameraView = ecs_world_view_t(world, CameraMovementView);
  for (EcsIterator* itr = ecs_view_itr(cameraView); ecs_view_walk(itr);) {
    GapWindowComp*           win   = ecs_view_write_t(itr, GapWindowComp);
    SceneTransformComp*      trans = ecs_view_write_t(itr, SceneTransformComp);
    SceneCameraMovementComp* move  = ecs_view_write_t(itr, SceneCameraMovementComp);

    SceneCameraInternalComp* internal = ecs_view_write_t(itr, SceneCameraInternalComp);
    if (!internal) {
      internal = ecs_world_add_t(world, ecs_view_entity(itr), SceneCameraInternalComp);
    }

    const bool boosted = gap_window_key_down(win, GapKey_Shift);
    const f32  posDelta =
        deltaSeconds * move->moveSpeed * (boosted ? g_cameraBoostMoveSpeedMult : 1.0f);

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
      const f32 deltaX = gap_window_param(win, GapParam_CursorDelta).x * g_cameraRotateSensitivity;
      const f32 deltaY = gap_window_param(win, GapParam_CursorDelta).y * g_cameraRotateSensitivity;

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
  ecs_register_comp_empty(SceneCameraSkyComp);

  ecs_register_view(GlobalTimeView);
  ecs_register_view(GlobalAssetsView);
  ecs_register_view(SkyView);
  ecs_register_view(CameraCreateView);
  ecs_register_view(CameraMovementView);

  ecs_register_system(SceneCameraCreateSys, ecs_view_id(CameraCreateView));
  ecs_register_system(SceneCameraCreateSkySys, ecs_view_id(GlobalAssetsView), ecs_view_id(SkyView));

  ecs_register_system(
      SceneCameraMovementSys, ecs_view_id(GlobalTimeView), ecs_view_id(CameraMovementView));
}

GeoMatrix scene_camera_proj(const SceneCameraComp* cam, const f32 aspect) {
  if (cam->flags & SceneCameraFlags_Vertical) {
    return geo_matrix_proj_pers_ver(cam->fov, aspect, cam->zNear);
  }
  return geo_matrix_proj_pers_hor(cam->fov, aspect, cam->zNear);
}
