#include "asset_manager.h"
#include "core_math.h"
#include "ecs_utils.h"
#include "ecs_world.h"
#include "gap_window.h"
#include "scene_camera.h"
#include "scene_renderable.h"
#include "scene_time.h"
#include "scene_transform.h"

static const f32       g_camMoveSpeed            = 10.0f;
static const f32       g_camMoveSpeedBoostMult   = 4.0f;
static const f32       g_camRotateSensitivity    = 0.0025f;
static const f32       g_camPersFov              = 60.0f * math_deg_to_rad;
static const f32       g_camPersFovMin           = 25.0f * math_deg_to_rad;
static const f32       g_camPersFovMax           = 120.0f * math_deg_to_rad;
static const f32       g_camPersZoomSensitivity  = 0.1f;
static const f32       g_camPersNear             = 0.1f;
static const GeoVector g_camPersPosition         = {0, 1.5f, -3.0f};
static const f32       g_camPersAngle            = 10 * math_deg_to_rad;
static const f32       g_camOrthoZoomSensitivity = 1.0f;
static const f32       g_camOrthoSize            = 5.0f;
static const f32       g_camOrthoSizeMin         = 0.1f;
static const f32       g_camOrthoSizeMax         = 1000.0f;
static const f32       g_camOrthoNear            = -1e4f;
static const f32       g_camOrthoFar             = +1e4f;

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

static void camera_update_move(
    const SceneCameraComp*         cam,
    const SceneCameraMovementComp* move,
    SceneTransformComp*            trans,
    const GapWindowComp*           win,
    const f32                      deltaSeconds) {
  const bool boosted   = gap_window_key_down(win, GapKey_Shift);
  const f32  moveSpeed = move->moveSpeed * (boosted ? g_camMoveSpeedBoostMult : 1.0f);
  const f32  posDelta  = deltaSeconds * moveSpeed;

  const GeoVector right   = geo_quat_rotate(trans->rotation, geo_right);
  const GeoVector up      = geo_quat_rotate(trans->rotation, geo_up);
  const GeoVector forward = geo_quat_rotate(trans->rotation, geo_forward);

  if (gap_window_key_down(win, GapKey_W)) {
    const GeoVector dir = cam->flags & SceneCameraFlags_Orthographic ? up : forward;
    trans->position     = geo_vector_add(trans->position, geo_vector_mul(dir, posDelta));
  }
  if (gap_window_key_down(win, GapKey_S)) {
    const GeoVector dir = cam->flags & SceneCameraFlags_Orthographic ? up : forward;
    trans->position     = geo_vector_sub(trans->position, geo_vector_mul(dir, posDelta));
  }
  if (gap_window_key_down(win, GapKey_D)) {
    trans->position = geo_vector_add(trans->position, geo_vector_mul(right, posDelta));
  }
  if (gap_window_key_down(win, GapKey_A)) {
    trans->position = geo_vector_sub(trans->position, geo_vector_mul(right, posDelta));
  }
}

static void camera_update_rotate(
    const SceneCameraMovementComp* move, SceneTransformComp* trans, const GapWindowComp* win) {

  const GeoVector right = geo_quat_rotate(trans->rotation, geo_right);

  const bool lookEnable = gap_window_key_down(win, GapKey_MouseRight) ||
                          gap_window_key_down(win, GapKey_Control) || move->locked;

  if (lookEnable) {
    const f32 deltaX = gap_window_param(win, GapParam_CursorDelta).x * g_camRotateSensitivity;
    const f32 deltaY = gap_window_param(win, GapParam_CursorDelta).y * g_camRotateSensitivity;

    trans->rotation = geo_quat_mul(geo_quat_angle_axis(right, deltaY), trans->rotation);
    trans->rotation = geo_quat_mul(geo_quat_angle_axis(geo_up, deltaX), trans->rotation);
    trans->rotation = geo_quat_norm(trans->rotation);
  }
}

static void camera_update_zoom(SceneCameraComp* cam, GapWindowComp* win) {
  const f32 scrollDelta = gap_window_param(win, GapParam_ScrollDelta).y;
  if (cam->flags & SceneCameraFlags_Orthographic) {
    const f32 delta = scrollDelta * g_camOrthoZoomSensitivity;
    cam->orthoSize  = math_clamp_f32(cam->orthoSize + delta, g_camOrthoSizeMin, g_camOrthoSizeMax);
  } else {
    const f32 delta = scrollDelta * g_camPersZoomSensitivity;
    cam->persFov    = math_clamp_f32(cam->persFov + delta, g_camPersFovMin, g_camPersFovMax);
  }
}

static void camera_update_lock(SceneCameraMovementComp* move, GapWindowComp* win) {
  if (gap_window_key_pressed(win, GapKey_Tab)) {
    if (move->locked) {
      gap_window_flags_unset(win, GapWindowFlags_CursorLock | GapWindowFlags_CursorHide);
    } else {
      gap_window_flags_set(win, GapWindowFlags_CursorLock | GapWindowFlags_CursorHide);
    }
    move->locked ^= true;
  }
}

static void camera_update_fullscreen(SceneCameraInternalComp* internal, GapWindowComp* win) {
  if (gap_window_key_pressed(win, GapKey_F)) {
    if (gap_window_mode(win) == GapWindowMode_Fullscreen) {
      gap_window_resize(win, internal->lastWindowedSize, GapWindowMode_Windowed);
    } else {
      internal->lastWindowedSize = gap_window_param(win, GapParam_WindowSize);
      gap_window_resize(win, gap_vector(0, 0), GapWindowMode_Fullscreen);
    }
  }
}

ecs_view_define(CameraUpdateView) {
  ecs_access_write(SceneCameraComp);
  ecs_access_write(SceneCameraMovementComp);
  ecs_access_write(SceneTransformComp);
  ecs_access_write(GapWindowComp);
  ecs_access_maybe_write(SceneCameraInternalComp);
}

ecs_system_define(SceneCameraUpdateSys) {
  EcsView*     view      = ecs_world_view_t(world, GlobalTimeView);
  EcsIterator* globalItr = ecs_view_maybe_at(view, ecs_world_global(world));
  if (!globalItr) {
    return;
  }
  const SceneTimeComp* time         = ecs_view_read_t(globalItr, SceneTimeComp);
  const f32            deltaSeconds = time->delta / (f32)time_second;

  EcsView* cameraView = ecs_world_view_t(world, CameraUpdateView);
  for (EcsIterator* itr = ecs_view_itr(cameraView); ecs_view_walk(itr);) {
    SceneCameraComp*         cam      = ecs_view_write_t(itr, SceneCameraComp);
    GapWindowComp*           win      = ecs_view_write_t(itr, GapWindowComp);
    SceneTransformComp*      trans    = ecs_view_write_t(itr, SceneTransformComp);
    SceneCameraMovementComp* move     = ecs_view_write_t(itr, SceneCameraMovementComp);
    SceneCameraInternalComp* internal = ecs_view_write_t(itr, SceneCameraInternalComp);
    if (!internal) {
      internal = ecs_world_add_t(world, ecs_view_entity(itr), SceneCameraInternalComp);
    }

    camera_update_move(cam, move, trans, win, deltaSeconds);
    camera_update_rotate(move, trans, win);
    camera_update_zoom(cam, win);
    camera_update_lock(move, win);
    camera_update_fullscreen(internal, win);

    if (gap_window_key_pressed(win, GapKey_F1)) {
      cam->flags &= ~SceneCameraFlags_Orthographic;
      cam->persFov    = g_camPersFov;
      trans->position = g_camPersPosition;
      trans->rotation = geo_quat_angle_axis(geo_right, g_camPersAngle);
    }
    if (gap_window_key_pressed(win, GapKey_F2)) {
      cam->flags |= SceneCameraFlags_Orthographic;
      trans->position = geo_vector(0);
      trans->rotation = geo_quat_look(geo_down, geo_forward);
    }
    if (gap_window_key_pressed(win, GapKey_F3)) {
      cam->flags ^= SceneCameraFlags_Vertical;
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
  ecs_register_view(CameraUpdateView);

  ecs_register_system(SceneCameraCreateSys, ecs_view_id(CameraCreateView));
  ecs_register_system(SceneCameraCreateSkySys, ecs_view_id(GlobalAssetsView), ecs_view_id(SkyView));

  ecs_register_system(
      SceneCameraUpdateSys, ecs_view_id(GlobalTimeView), ecs_view_id(CameraUpdateView));
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
