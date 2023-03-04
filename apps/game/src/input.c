#include "asset_manager.h"
#include "core_array.h"
#include "core_format.h"
#include "core_math.h"
#include "debug_stats.h"
#include "ecs_world.h"
#include "geo_plane.h"
#include "input_manager.h"
#include "scene_attachment.h"
#include "scene_camera.h"
#include "scene_collision.h"
#include "scene_lifetime.h"
#include "scene_nav.h"
#include "scene_selection.h"
#include "scene_terrain.h"
#include "scene_time.h"
#include "scene_transform.h"
#include "scene_vfx.h"
#include "ui.h"

#include "cmd_internal.h"

static const f32    g_inputMinInteractDist       = 1.0f;
static const f32    g_inputMaxInteractDist       = 250.0f;
static const f32    g_inputCamDistMin            = 20.0f;
static const f32    g_inputCamDistMax            = 85.0f;
static const f32    g_inputCamPanCursorMult      = 100.0f;
static const f32    g_inputCamPanTriggeredMult   = 50.0f;
static const f32    g_inputCamPanMaxZoomMult     = 0.4f;
static const f32    g_inputCamPosEaseSpeed       = 20.0f;
static const f32    g_inputCamRotX               = 65.0f * math_deg_to_rad;
static const f32    g_inputCamRotYMult           = 5.0f;
static const f32    g_inputCamRotYEaseSpeed      = 20.0f;
static const f32    g_inputCamZoomMult           = 0.1f;
static const f32    g_inputCamZoomEaseSpeed      = 15.0f;
static const f32    g_inputCamCursorPanThreshold = 0.0025f;
static const GeoBox g_inputCamArea               = {.min = {-100, 0, -100}, .max = {100, 0, 100}};
static const f32    g_inputDragThreshold         = 0.005f; // In normalized screen-space coords.
static const String g_inputMoveVfxAsset          = string_static("vfx/game/indicator_move.vfx");
static const String g_inputAttackVfxAsset        = string_static("vfx/game/indicator_attack.vfx");

typedef enum {
  InputSelectState_None,
  InputSelectState_Blocked,
  InputSelectState_Down,
  InputSelectState_Dragging,
} InputSelectState;

ecs_comp_define(InputStateComp) {
  EcsEntityId      uiCanvas;
  InputSelectState selectState;
  GeoVector        selectStart; // NOTE: Normalized screen-space x,y coordinates.
  bool             freeCamera;

  u32 lastSelectionCount;

  GeoVector camPos, camPosTgt;
  f32       camRotY, camRotYTgt;
  f32       camZoom, camZoomTgt;
};

static void input_report_command(DebugStatsGlobalComp* debugStats, const String command) {
  if (debugStats) {
    const String label = string_lit("Command");
    debug_stats_notify(debugStats, label, command);
  }
}

static void input_report_selection_count(DebugStatsGlobalComp* debugStats, const u32 selCount) {
  if (debugStats) {
    const String label = string_lit("Selected");
    debug_stats_notify(debugStats, label, fmt_write_scratch("{}", fmt_int(selCount)));
  }
}

static void input_indicator_move(EcsWorld* world, AssetManagerComp* assets, const GeoVector pos) {
  const EcsEntityId vfxAsset  = asset_lookup(world, assets, g_inputMoveVfxAsset);
  const EcsEntityId vfxEntity = ecs_world_entity_create(world);
  const GeoQuat     rot       = geo_quat_ident;
  ecs_world_add_t(world, vfxEntity, SceneTransformComp, .position = pos, .rotation = rot);
  ecs_world_add_t(world, vfxEntity, SceneLifetimeDurationComp, .duration = time_second);
  ecs_world_add_t(world, vfxEntity, SceneVfxComp, .asset = vfxAsset, .alpha = 1.0f);
}

static void input_indicator_attack(EcsWorld* world, AssetManagerComp* assets, const EcsEntityId t) {
  const EcsEntityId vfxAsset  = asset_lookup(world, assets, g_inputAttackVfxAsset);
  const EcsEntityId vfxEntity = ecs_world_entity_create(world);
  ecs_world_add_t(world, vfxEntity, SceneTransformComp, .rotation = geo_quat_ident);
  ecs_world_add_t(world, vfxEntity, SceneAttachmentComp, .target = t);
  ecs_world_add_t(world, vfxEntity, SceneLifetimeDurationComp, .duration = time_second);
  ecs_world_add_t(world, vfxEntity, SceneVfxComp, .asset = vfxAsset, .alpha = 1.0f);
}

static void update_camera_movement(
    InputStateComp*      state,
    InputManagerComp*    input,
    const SceneTimeComp* time,
    SceneTransformComp*  camTrans) {
  const f32     deltaSeconds = scene_real_delta_seconds(time);
  const GeoQuat camRotYOld   = geo_quat_from_euler(geo_vector(0, state->camRotY, 0));
  bool          lockCursor   = false;

  // Update pan.
  GeoVector panDeltaRel = {0};
  if (!lockCursor && input_triggered_lit(input, "CameraPanCursor")) {
    const f32 panX = -input_cursor_delta_x(input);
    const f32 panY = -input_cursor_delta_y(input);
    panDeltaRel    = geo_vector_mul(geo_vector(panX, 0, panY), g_inputCamPanCursorMult);
    lockCursor     = true;
  } else {
    // clang-format off
    if (input_triggered_lit(input, "CameraPanForward"))  { panDeltaRel.z += 1; }
    if (input_triggered_lit(input, "CameraPanBackward")) { panDeltaRel.z -= 1; }
    if (input_triggered_lit(input, "CameraPanRight"))    { panDeltaRel.x += 1; }
    if (input_triggered_lit(input, "CameraPanLeft"))     { panDeltaRel.x -= 1; }
    if (input_blockers(input) & InputBlocker_CursorConfined) {
      const f32 cursorX = input_cursor_x(input), cursorY = input_cursor_y(input);
      if(cursorY >= (1.0f - g_inputCamCursorPanThreshold)) { panDeltaRel.z += 1; }
      if(cursorY <= g_inputCamCursorPanThreshold)          { panDeltaRel.z -= 1; }
      if(cursorX >= (1.0f - g_inputCamCursorPanThreshold)) { panDeltaRel.x += 1; }
      if(cursorX <= g_inputCamCursorPanThreshold)          { panDeltaRel.x -= 1; }
    }
    // clang-format on
    if (geo_vector_mag_sqr(panDeltaRel) > 0) {
      const GeoVector moveDirRel = geo_vector_norm(panDeltaRel);
      panDeltaRel = geo_vector_mul(moveDirRel, deltaSeconds * g_inputCamPanTriggeredMult);
    }
  }
  panDeltaRel = geo_vector_mul(panDeltaRel, math_lerp(1, g_inputCamPanMaxZoomMult, state->camZoom));
  const f32 camPosEaseDelta = deltaSeconds * g_inputCamPosEaseSpeed;
  state->camPosTgt = geo_vector_add(state->camPosTgt, geo_quat_rotate(camRotYOld, panDeltaRel));
  state->camPosTgt = geo_box_closest_point(&g_inputCamArea, state->camPosTgt);
  state->camPos    = geo_vector_lerp(state->camPos, state->camPosTgt, camPosEaseDelta);

  // Update Y rotation.
  if (!lockCursor && input_triggered_lit(input, "CameraRotate")) {
    const f32 rotDelta = input_cursor_delta_x(input) * g_inputCamRotYMult;
    state->camRotYTgt  = math_mod_f32(state->camRotYTgt + rotDelta, math_pi_f32 * 2.0f);
    lockCursor         = true;
  }
  const f32 camRotEaseDelta = deltaSeconds * g_inputCamRotYEaseSpeed;
  state->camRotY = math_lerp_angle_f32(state->camRotY, state->camRotYTgt, camRotEaseDelta);

  // Update zoom.
  if ((input_blockers(input) & InputBlocker_HoveringUi) == 0) {
    const f32 zoomDelta = input_scroll_y(input) * g_inputCamZoomMult;
    state->camZoomTgt   = math_clamp_f32(state->camZoomTgt + zoomDelta, 0.0f, 1.0f);
  }
  const f32 camZoomEaseDelta = deltaSeconds * g_inputCamZoomEaseSpeed;
  state->camZoom             = math_lerp(state->camZoom, state->camZoomTgt, camZoomEaseDelta);

  // Set camera transform.
  const GeoQuat   camRot    = geo_quat_from_euler(geo_vector(g_inputCamRotX, state->camRotY, 0));
  const f32       camDist   = math_lerp(g_inputCamDistMax, g_inputCamDistMin, state->camZoom);
  const GeoVector camOffset = geo_quat_rotate(camRot, geo_vector(0, 0, -camDist));
  camTrans->position        = geo_vector_add(state->camPos, camOffset);
  camTrans->rotation        = camRot;

  input_cursor_mode_set(input, lockCursor ? InputCursorMode_Locked : InputCursorMode_Normal);
}

static void update_camera_movement_free(
    InputManagerComp*      input,
    const SceneTimeComp*   time,
    const SceneCameraComp* camera,
    SceneTransformComp*    camTrans) {
  const f32       deltaSeconds = scene_real_delta_seconds(time);
  const GeoVector camRight     = geo_quat_rotate(camTrans->rotation, geo_right);
  bool            lockCursor   = false;

  static const f32 g_panSpeed          = 20.0f;
  static const f32 g_rotateSensitivity = 4.0f;

  GeoVector panDelta = {0};
  // clang-format off
  if (input_triggered_lit(input, "CameraPanForward"))  { panDelta.z += 1; }
  if (input_triggered_lit(input, "CameraPanBackward")) { panDelta.z -= 1; }
  if (input_triggered_lit(input, "CameraPanRight"))    { panDelta.x += 1; }
  if (input_triggered_lit(input, "CameraPanLeft"))     { panDelta.x -= 1; }
  // clang-format on
  if (geo_vector_mag_sqr(panDelta) > 0) {
    panDelta = geo_vector_mul(geo_vector_norm(panDelta), deltaSeconds * g_panSpeed);
    if (camera->flags & SceneCameraFlags_Orthographic) {
      panDelta.y = panDelta.z;
      panDelta.z = 0;
    }
    panDelta           = geo_quat_rotate(camTrans->rotation, panDelta);
    camTrans->position = geo_vector_add(camTrans->position, panDelta);
  }

  if (input_triggered_lit(input, "CameraRotate")) {
    const f32 deltaX = input_cursor_delta_x(input) * g_rotateSensitivity;
    const f32 deltaY = input_cursor_delta_y(input) * -g_rotateSensitivity;

    camTrans->rotation = geo_quat_mul(geo_quat_angle_axis(camRight, deltaY), camTrans->rotation);
    camTrans->rotation = geo_quat_mul(geo_quat_angle_axis(geo_up, deltaX), camTrans->rotation);
    camTrans->rotation = geo_quat_norm(camTrans->rotation);
    lockCursor         = true;
  }

  input_cursor_mode_set(input, lockCursor ? InputCursorMode_Locked : InputCursorMode_Normal);
}

static void select_start(InputStateComp* state, InputManagerComp* input) {
  state->selectState = InputSelectState_Down;
  state->selectStart = (GeoVector){.x = input_cursor_x(input), .y = input_cursor_y(input)};
}

static void select_start_drag(InputStateComp* state) {
  state->selectState = InputSelectState_Dragging;
}

static void select_end_click(
    InputStateComp*              state,
    CmdControllerComp*           cmdController,
    InputManagerComp*            input,
    const SceneCollisionEnvComp* collisionEnv,
    const GeoRay*                inputRay) {
  state->selectState = InputSelectState_None;

  SceneRayHit            hit;
  const SceneQueryFilter filter  = {.layerMask = SceneLayer_All};
  const f32              maxDist = 1e4f;
  const bool             hasHit  = scene_query_ray(collisionEnv, inputRay, maxDist, &filter, &hit);

  const bool addToSelection      = (input_modifiers(input) & InputModifier_Control) != 0;
  const bool removeFromSelection = (input_modifiers(input) & InputModifier_Shift) != 0;
  if (hasHit) {
    if (!addToSelection && !removeFromSelection) {
      cmd_push_deselect_all(cmdController);
    }
    if (removeFromSelection) {
      cmd_push_deselect(cmdController, hit.entity);
    } else {
      cmd_push_select(cmdController, hit.entity);
    }
  } else if (!addToSelection && !removeFromSelection) {
    cmd_push_deselect_all(cmdController);
  }
}

static void select_update_drag(
    InputStateComp*              state,
    InputManagerComp*            input,
    CmdControllerComp*           cmdController,
    const SceneCollisionEnvComp* collisionEnv,
    const SceneCameraComp*       camera,
    const SceneTransformComp*    cameraTrans,
    const f32                    inputAspect) {
  const bool addToSelection      = (input_modifiers(input) & InputModifier_Control) != 0;
  const bool removeFromSelection = (input_modifiers(input) & InputModifier_Shift) != 0;
  if (!addToSelection && !removeFromSelection) {
    cmd_push_deselect_all(cmdController);
  }

  const GeoVector cur = {.x = input_cursor_x(input), .y = input_cursor_y(input)};
  const GeoVector min = geo_vector_min(state->selectStart, cur);
  const GeoVector max = geo_vector_max(state->selectStart, cur);
  if (min.x == max.x || min.y == max.y) {
    return;
  }
  GeoVector frustumCorners[8];
  scene_camera_frustum_corners(camera, cameraTrans, inputAspect, min, max, frustumCorners);

  // Only allow box-selecting your own units.
  const SceneQueryFilter filter = {.layerMask = SceneLayer_UnitFactionA};

  EcsEntityId results[scene_query_max_hits];
  const u32   resultCount = scene_query_frustum_all(collisionEnv, frustumCorners, &filter, results);
  for (u32 i = 0; i != resultCount; ++i) {
    if (removeFromSelection) {
      cmd_push_deselect(cmdController, results[i]);
    } else {
      cmd_push_select(cmdController, results[i]);
    }
  }
}

static void select_end_drag(InputStateComp* state) { state->selectState = InputSelectState_None; }

static void input_order_attack(
    EcsWorld*                 world,
    CmdControllerComp*        cmdController,
    const SceneSelectionComp* sel,
    AssetManagerComp*         assets,
    DebugStatsGlobalComp*     debugStats,
    const EcsEntityId         target) {

  // Report the attack.
  input_indicator_attack(world, assets, target);
  input_report_command(debugStats, string_lit("Attack"));

  // Push attack commands.
  for (const EcsEntityId* e = scene_selection_begin(sel); e != scene_selection_end(sel); ++e) {
    cmd_push_attack(cmdController, *e, target);
  }
}

static void input_order_move(
    EcsWorld*                 world,
    CmdControllerComp*        cmdController,
    const SceneSelectionComp* sel,
    const SceneNavEnvComp*    nav,
    AssetManagerComp*         assets,
    DebugStatsGlobalComp*     debugStats,
    const GeoVector           targetPos) {

  // Report the move.
  input_indicator_move(world, assets, targetPos);
  input_report_command(debugStats, string_lit("Move"));

  // Find unblocked cells on the nav-grid to move to.
  const u32                 selectionCount = scene_selection_count(sel);
  GeoNavCell                navCells[1024];
  const GeoNavCellContainer navCellContainer = {
      .cells    = navCells,
      .capacity = math_min(selectionCount, array_elems(navCells)),
  };
  const GeoNavCell targetNavCell = scene_nav_at_position(nav, targetPos);
  const u32 unblockedCount = scene_nav_closest_unblocked_n(nav, targetNavCell, navCellContainer);

  // Push the move commands.
  for (u32 i = 0; i != selectionCount; ++i) {
    const EcsEntityId entity = scene_selection_begin(sel)[i];
    GeoVector         pos;
    if (LIKELY(i < unblockedCount)) {
      const bool sameCellAsTargetPos = navCells[i].data == targetNavCell.data;
      pos = sameCellAsTargetPos ? targetPos : scene_nav_position(nav, navCells[i]);
    } else {
      // We didn't find a free cell for this entity; just move to the raw targetPos.
      pos = targetPos;
    }
    cmd_push_move(cmdController, entity, pos);
  }
}

static void input_order_stop(
    CmdControllerComp*        cmdController,
    const SceneSelectionComp* sel,
    DebugStatsGlobalComp*     debugStats) {

  // Report the stop.
  input_report_command(debugStats, string_lit("Stop"));

  // Push the stop commands.
  for (const EcsEntityId* e = scene_selection_begin(sel); e != scene_selection_end(sel); ++e) {
    cmd_push_stop(cmdController, *e);
  }
}

static void input_order_destroy(
    CmdControllerComp*        cmdController,
    const SceneSelectionComp* sel,
    DebugStatsGlobalComp*     debugStats) {

  // Report the destroy.
  input_report_command(debugStats, string_lit("Destroy"));

  // Push the destroy commands.
  for (const EcsEntityId* e = scene_selection_begin(sel); e != scene_selection_end(sel); ++e) {
    cmd_push_destroy(cmdController, *e);
  }
}

static void input_order(
    EcsWorld*                    world,
    CmdControllerComp*           cmdController,
    const SceneCollisionEnvComp* collisionEnv,
    const SceneSelectionComp*    sel,
    const SceneTerrainComp*      terrain,
    const SceneNavEnvComp*       nav,
    AssetManagerComp*            assets,
    DebugStatsGlobalComp*        debugStats,
    const GeoRay*                inputRay) {
  /**
   * Order an attack when clicking an opponent unit.
   */
  SceneRayHit            hit;
  const SceneQueryFilter filter  = {.layerMask = ~SceneLayer_UnitFactionA & SceneLayer_Unit};
  const f32              radius  = 0.5f;
  const f32              maxDist = g_inputMaxInteractDist;
  if (scene_query_ray_fat(collisionEnv, inputRay, radius, maxDist, &filter, &hit)) {
    input_order_attack(world, cmdController, sel, assets, debugStats, hit.entity);
    return;
  }
  /**
   * Order a move when clicking the terrain.
   */
  if (terrain) {
    const f32 rayT = scene_terrain_intersect_ray(terrain, inputRay, g_inputMaxInteractDist);
    if (rayT > g_inputMinInteractDist) {
      const GeoVector targetPos = geo_ray_position(inputRay, rayT);
      input_order_move(world, cmdController, sel, nav, assets, debugStats, targetPos);
      return;
    }
  }
}

static void update_camera_interact(
    EcsWorld*                    world,
    InputStateComp*              state,
    CmdControllerComp*           cmdController,
    InputManagerComp*            input,
    const SceneCollisionEnvComp* collisionEnv,
    const SceneSelectionComp*    sel,
    const SceneTerrainComp*      terrain,
    const SceneNavEnvComp*       nav,
    const SceneCameraComp*       camera,
    const SceneTransformComp*    cameraTrans,
    AssetManagerComp*            assets,
    DebugStatsGlobalComp*        debugStats) {
  const GeoVector inputNormPos = geo_vector(input_cursor_x(input), input_cursor_y(input));
  const f32       inputAspect  = input_cursor_aspect(input);
  const GeoRay    inputRay     = scene_camera_ray(camera, cameraTrans, inputAspect, inputNormPos);

  const bool selectActive = input_triggered_lit(input, "Select");
  switch (state->selectState) {
  case InputSelectState_None:
    if (input_blockers(input) & (InputBlocker_HoveringUi | InputBlocker_HoveringGizmo)) {
      state->selectState = InputSelectState_Blocked;
    } else if (selectActive) {
      select_start(state, input);
    }
    break;
  case InputSelectState_Blocked:
    if (!selectActive) {
      state->selectState = InputSelectState_None;
    }
    break;
  case InputSelectState_Down:
    if (selectActive) {
      const GeoVector cur = {.x = input_cursor_x(input), .y = input_cursor_y(input)};
      if (geo_vector_mag(geo_vector_sub(cur, state->selectStart)) > g_inputDragThreshold) {
        select_start_drag(state);
      }
    } else {
      select_end_click(state, cmdController, input, collisionEnv, &inputRay);
    }
    break;
  case InputSelectState_Dragging:
    if (selectActive) {
      select_update_drag(
          state, input, cmdController, collisionEnv, camera, cameraTrans, inputAspect);
    } else {
      select_end_drag(state);
    }
    break;
  }

  const bool hasSelection = !scene_selection_empty(sel);
  if (!selectActive && hasSelection && input_triggered_lit(input, "Order")) {
    input_order(
        world, cmdController, collisionEnv, sel, terrain, nav, assets, debugStats, &inputRay);
  }
  if (input_triggered_lit(input, "CameraReset")) {
    state->freeCamera = false;
    state->camPosTgt  = geo_vector(0);
    state->camRotYTgt = -90.0f * math_deg_to_rad;
    state->camZoomTgt = 0.0f;
    input_report_command(debugStats, string_lit("Reset camera"));
  }
}

static void input_state_init(EcsWorld* world, const EcsEntityId windowEntity) {
  const f32 camStartRotY = -90.0f * math_deg_to_rad;
  ecs_world_add_t(
      world,
      windowEntity,
      InputStateComp,
      .uiCanvas   = ui_canvas_create(world, windowEntity, UiCanvasCreateFlags_ToBack),
      .camRotY    = camStartRotY,
      .camRotYTgt = camStartRotY);
}

ecs_view_define(GlobalUpdateView) {
  ecs_access_maybe_read(SceneTerrainComp);
  ecs_access_maybe_write(DebugStatsGlobalComp);
  ecs_access_read(SceneCollisionEnvComp);
  ecs_access_read(SceneNavEnvComp);
  ecs_access_read(SceneSelectionComp);
  ecs_access_read(SceneTimeComp);
  ecs_access_write(AssetManagerComp);
  ecs_access_write(CmdControllerComp);
  ecs_access_write(InputManagerComp);
}

ecs_view_define(CameraView) {
  ecs_access_maybe_write(InputStateComp);
  ecs_access_read(SceneCameraComp);
  ecs_access_write(SceneTransformComp);
}

static void input_toggle_camera_mode(InputStateComp* state, DebugStatsGlobalComp* debugStats) {
  state->freeCamera ^= true;
  if (debugStats) {
    debug_stats_notify(
        debugStats,
        string_lit("Camera Mode"),
        state->freeCamera ? string_lit("Free") : string_lit("Normal"));
  }
}

ecs_system_define(InputUpdateSys) {
  EcsView*     globalView = ecs_world_view_t(world, GlobalUpdateView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (!globalItr) {
    return;
  }
  CmdControllerComp*           cmdController = ecs_view_write_t(globalItr, CmdControllerComp);
  const SceneCollisionEnvComp* colEnv        = ecs_view_read_t(globalItr, SceneCollisionEnvComp);
  const SceneSelectionComp*    sel           = ecs_view_read_t(globalItr, SceneSelectionComp);
  const SceneTerrainComp*      terrain       = ecs_view_read_t(globalItr, SceneTerrainComp);
  const SceneNavEnvComp*       nav           = ecs_view_read_t(globalItr, SceneNavEnvComp);
  const SceneTimeComp*         time          = ecs_view_read_t(globalItr, SceneTimeComp);
  InputManagerComp*            input         = ecs_view_write_t(globalItr, InputManagerComp);
  AssetManagerComp*            assets        = ecs_view_write_t(globalItr, AssetManagerComp);
  DebugStatsGlobalComp*        debugStats    = ecs_view_write_t(globalItr, DebugStatsGlobalComp);

  if (input_triggered_lit(input, "OrderStop")) {
    input_order_stop(cmdController, sel, debugStats);
  }
  if (input_triggered_lit(input, "Destroy")) {
    input_order_destroy(cmdController, sel, debugStats);
  }

  EcsView* cameraView = ecs_world_view_t(world, CameraView);
  for (EcsIterator* itr = ecs_view_itr(cameraView); ecs_view_walk(itr);) {
    EcsIterator*           camItr   = ecs_view_at(cameraView, ecs_view_entity(itr));
    const SceneCameraComp* cam      = ecs_view_read_t(camItr, SceneCameraComp);
    SceneTransformComp*    camTrans = ecs_view_write_t(camItr, SceneTransformComp);
    InputStateComp*        state    = ecs_view_write_t(camItr, InputStateComp);
    if (!state) {
      input_state_init(world, ecs_view_entity(camItr));
      continue;
    }

    if (scene_selection_count(sel) != state->lastSelectionCount) {
      state->lastSelectionCount = scene_selection_count(sel);
      input_report_selection_count(debugStats, state->lastSelectionCount);
    }

    if (input_active_window(input) == ecs_view_entity(itr)) {
      if (input_triggered_lit(input, "CameraToggleMode")) {
        input_toggle_camera_mode(state, debugStats);
      }
      if (state->freeCamera) {
        update_camera_movement_free(input, time, cam, camTrans);
      } else {
        update_camera_movement(state, input, time, camTrans);
      }
      update_camera_interact(
          world,
          state,
          cmdController,
          input,
          colEnv,
          sel,
          terrain,
          nav,
          cam,
          camTrans,
          assets,
          debugStats);
    } else {
      state->selectState = InputSelectState_None;
    }
  }
}

ecs_view_define(UiCameraView) { ecs_access_write(InputStateComp); }
ecs_view_define(UiCanvasView) { ecs_access_write(UiCanvasComp); }

ecs_system_define(InputDrawUiSys) {
  EcsIterator* canvasItr  = ecs_view_itr(ecs_world_view_t(world, UiCanvasView));
  EcsView*     cameraView = ecs_world_view_t(world, UiCameraView);
  for (EcsIterator* itr = ecs_view_itr(cameraView); ecs_view_walk(itr);) {
    InputStateComp* state = ecs_view_write_t(itr, InputStateComp);
    if (!ecs_view_maybe_jump(canvasItr, state->uiCanvas)) {
      continue;
    }
    UiCanvasComp* canvas = ecs_view_write_t(canvasItr, UiCanvasComp);
    ui_canvas_reset(canvas);
    ui_canvas_to_back(canvas);

    if (state->selectState == InputSelectState_Dragging) {
      const UiVector startPos = ui_vector(state->selectStart.x, state->selectStart.y);
      ui_layout_move(canvas, startPos, UiBase_Canvas, Ui_XY);
      ui_layout_resize_to(canvas, UiBase_Input, UiAlign_BottomLeft, Ui_XY);
      ui_style_color(canvas, ui_color(255, 255, 255, 16));
      ui_style_outline(canvas, 3);
      ui_canvas_draw_glyph(canvas, UiShape_Square, 10, UiFlags_None);
    }
  }
}

ecs_module_init(game_input_module) {
  ecs_register_comp(InputStateComp);

  ecs_register_view(GlobalUpdateView);
  ecs_register_view(CameraView);
  ecs_register_view(UiCameraView);
  ecs_register_view(UiCanvasView);

  ecs_register_system(InputUpdateSys, ecs_view_id(GlobalUpdateView), ecs_view_id(CameraView));
  ecs_register_system(InputDrawUiSys, ecs_view_id(UiCameraView), ecs_view_id(UiCanvasView));

  enum {
    Order_Normal      = 0,
    Order_InputDrawUi = 1,
  };
  ecs_order(InputDrawUiSys, Order_InputDrawUi);
}
