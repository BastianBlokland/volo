#include "core/array.h"
#include "core/format.h"
#include "core/math.h"
#include "ecs/view.h"
#include "ecs/world.h"
#include "input/manager.h"
#include "scene/attachment.h"
#include "scene/camera.h"
#include "scene/collision.h"
#include "scene/level.h"
#include "scene/nav.h"
#include "scene/prefab.h"
#include "scene/product.h"
#include "scene/set.h"
#include "scene/terrain.h"
#include "scene/time.h"
#include "scene/transform.h"
#include "ui/canvas.h"
#include "ui/color.h"
#include "ui/layout.h"
#include "ui/shape.h"
#include "ui/style.h"

#include "cmd.h"
#include "hud.h"
#include "input.h"

static const f32  g_inputInteractMinDist       = 1.0f;
static const f32  g_inputInteractMaxDist       = 250.0f;
static const f32  g_inputInteractRadius        = 0.5f;
static const f32  g_inputCamDistMin            = 20.0f;
static const f32  g_inputCamDistMax            = 85.0f;
static const f32  g_inputCamPanCursorMult      = 100.0f;
static const f32  g_inputCamPanTriggeredMult   = 50.0f;
static const f32  g_inputCamPanMaxZoomMult     = 0.4f;
static const f32  g_inputCamPosEaseSpeed       = 20.0f;
static const f32  g_inputCamRotX               = 65.0f * math_deg_to_rad;
static const f32  g_inputCamRotYMult           = 5.0f;
static const f32  g_inputCamRotYEaseSpeed      = 20.0f;
static const f32  g_inputCamZoomMult           = 0.1f;
static const f32  g_inputCamZoomEaseSpeed      = 15.0f;
static const f32  g_inputCamCursorPanThreshold = 0.0025f;
static const f32  g_inputDragThreshold         = 0.005f; // In normalized screen-space coords.
static StringHash g_inputGroupActions[game_cmd_group_count];

typedef enum {
  InputFlags_AllowZoomOverUi = 1 << 0,
  InputFlags_SnapCamera      = 1 << 1,
} InputFlags;

typedef enum {
  InputSelectState_None,
  InputSelectState_Blocked,
  InputSelectState_Down,
  InputSelectState_Dragging,
} InputSelectState;

typedef enum {
  InputSelectMode_Replace,
  InputSelectMode_Add,
  InputSelectMode_Subtract,
} InputSelectMode;

typedef enum {
  InputQuery_Select,
  InputQuery_Attack,

  InputQuery_Count
} InputQueryType;

ecs_comp_define(GameInputComp) {
  EcsEntityId      uiCanvas;
  GameInputType    type : 8;
  InputFlags       flags : 8;
  InputSelectState selectState : 8;
  InputSelectMode  selectMode : 8;
  u32              lastLevelCounter;
  GeoVector        selectStart; // NOTE: Normalized screen-space x,y coordinates.

  StringHash   lastGroupAction;
  TimeDuration lastGroupTime;

  u32 lastSelectionCount;

  EcsEntityId  hoveredEntity[InputQuery_Count];
  TimeDuration hoveredTime[InputQuery_Count];

  GeoVector camPos, camPosTgt;
  f32       camRotY, camRotYTgt;
  f32       camZoom, camZoomTgt;
};

static SceneQueryFilter input_query_filter(const InputManagerComp* input, const InputQueryType t) {
  SceneQueryFilter filter = {0};
  switch (t) {
  case InputQuery_Select:
    if (input_layer_active(input, string_hash_lit("Dev"))) {
      // Allow selecting all objects (including debug shapes) in development mode.
      filter.layerMask = SceneLayer_AllIncludingDebug;
    } else {
      // In node mode only allow selecting your own units.
      filter.layerMask = SceneLayer_UnitFactionA;
    }
    break;
  case InputQuery_Attack:
    filter.layerMask = (~SceneLayer_UnitFactionA & SceneLayer_Unit) | SceneLayer_Destructible;
    break;
  case InputQuery_Count:
    break;
  }
  return filter;
}

static EcsEntityId input_query_ray(
    const SceneCollisionEnvComp* collisionEnv,
    const InputManagerComp*      input,
    const InputQueryType         t,
    const GeoRay*                inputRay) {

  const SceneQueryFilter filter = input_query_filter(input, t);
  const f32              radius = g_inputInteractRadius;

  SceneRayHit hit;
  if (scene_query_ray_fat(collisionEnv, inputRay, radius, g_inputInteractMaxDist, &filter, &hit)) {
    return hit.time >= g_inputInteractMinDist ? hit.entity : 0;
  }
  return 0;
}

static void input_indicator_move(EcsWorld* world, const GeoVector pos) {
  scene_prefab_spawn(
      world,
      &(ScenePrefabSpec){
          .flags    = ScenePrefabFlags_Volatile,
          .prefabId = string_hash_lit("EffectIndicatorMove"),
          .faction  = SceneFaction_None,
          .position = pos,
          .rotation = geo_quat_ident});
}

static void input_indicator_attack(EcsWorld* world, const EcsEntityId target) {
  const EcsEntityId effectEntity = scene_prefab_spawn(
      world,
      &(ScenePrefabSpec){
          .flags    = ScenePrefabFlags_Volatile,
          .prefabId = string_hash_lit("EffectIndicatorAttack"),
          .faction  = SceneFaction_None,
          .rotation = geo_quat_ident});

  scene_attach_to_entity(world, effectEntity, target);
}

static GeoVector input_clamp_to_play_area(const SceneTerrainComp* terrain, const GeoVector pos) {
  if (scene_terrain_loaded(terrain)) {
    const GeoBox area = scene_terrain_play_bounds(terrain);
    return geo_box_closest_point(&area, pos);
  }
  return pos;
}

static void update_group_input(
    GameInputComp*         state,
    GameCmdComp*           cmd,
    InputManagerComp*      input,
    const SceneSetEnvComp* setEnv,
    const SceneTimeComp*   time) {
  for (u32 i = 0; i != game_cmd_group_count; ++i) {
    if (!input_triggered_hash(input, g_inputGroupActions[i])) {
      continue;
    }
    const bool doublePress =
        state->lastGroupAction == g_inputGroupActions[i] &&
        (time->realTime - state->lastGroupTime) < input_doubleclick_interval(input);

    state->lastGroupAction = g_inputGroupActions[i];
    state->lastGroupTime   = time->realTime;

    if (input_modifiers(input) & InputModifier_Control) {
      // Assign the current selection to this group.
      game_cmd_group_clear(cmd, i);
      const StringHash s = g_sceneSetSelected;
      for (const EcsEntityId* e = scene_set_begin(setEnv, s); e != scene_set_end(setEnv, s); ++e) {
        game_cmd_group_add(cmd, i, *e);
      }
    } else {
      game_cmd_push_select_group(cmd, i);
    }

    if (doublePress && game_cmd_group_size(cmd, i)) {
      state->camPosTgt = game_cmd_group_position(cmd, i);
    }
  }
}

static void update_camera_movement(
    GameInputComp*          state,
    InputManagerComp*       input,
    const SceneTimeComp*    time,
    const SceneTerrainComp* terrain,
    SceneTransformComp*     camTrans,
    const bool              windowActive) {
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
  const f32 camPosEaseDelta = math_min(deltaSeconds * g_inputCamPosEaseSpeed, 1.0f);
  state->camPosTgt = geo_vector_add(state->camPosTgt, geo_quat_rotate(camRotYOld, panDeltaRel));
  state->camPosTgt = input_clamp_to_play_area(terrain, state->camPosTgt);
  if (state->flags & InputFlags_SnapCamera) {
    state->camPos = state->camPosTgt;
  } else {
    state->camPos = geo_vector_lerp(state->camPos, state->camPosTgt, camPosEaseDelta);
  }

  // Update Y rotation.
  if (!lockCursor && input_triggered_lit(input, "CameraRotate")) {
    const f32 rotDelta = input_cursor_delta_x(input) * g_inputCamRotYMult;
    state->camRotYTgt  = math_mod_f32(state->camRotYTgt + rotDelta, math_pi_f32 * 2.0f);
    lockCursor         = true;
  }
  const f32 camRotEaseDelta = math_min(1.0f, deltaSeconds * g_inputCamRotYEaseSpeed);
  if (state->flags & InputFlags_SnapCamera) {
    state->camRotY = state->camRotYTgt;
  } else {
    state->camRotY = math_lerp_angle_f32(state->camRotY, state->camRotYTgt, camRotEaseDelta);
  }

  // Update zoom.
  if (windowActive) { /* Disallow zooming when the window is not focussed. */
    const bool isHoveringUi = (input_blockers(input) & InputBlocker_HoveringUi) != 0;
    if (!isHoveringUi || state->flags & InputFlags_AllowZoomOverUi) {
      const f32 zoomDelta = input_scroll_y(input) * g_inputCamZoomMult;
      state->camZoomTgt   = math_clamp_f32(state->camZoomTgt + zoomDelta, 0.0f, 1.0f);
    }
    if (state->flags & InputFlags_SnapCamera) {
      state->camZoom = state->camZoomTgt;
    } else {
      const f32 camZoomEaseDelta = math_min(1.0f, deltaSeconds * g_inputCamZoomEaseSpeed);
      state->camZoom             = math_lerp(state->camZoom, state->camZoomTgt, camZoomEaseDelta);
    }
  }

  // Set camera transform.
  const GeoQuat   camRot    = geo_quat_from_euler(geo_vector(g_inputCamRotX, state->camRotY, 0));
  const f32       camDist   = math_lerp(g_inputCamDistMax, g_inputCamDistMin, state->camZoom);
  const GeoVector camOffset = geo_quat_rotate(camRot, geo_vector(0, 0, -camDist));
  camTrans->position        = geo_vector_add(state->camPos, camOffset);
  camTrans->rotation        = camRot;

  input_cursor_mode_set(input, lockCursor ? InputCursorMode_Locked : InputCursorMode_Normal);
  state->flags &= ~InputFlags_SnapCamera;
}

static void update_camera_movement_dev(
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

    camTrans->rotation = geo_quat_mul(geo_quat_angle_axis(deltaY, camRight), camTrans->rotation);
    camTrans->rotation = geo_quat_mul(geo_quat_angle_axis(deltaX, geo_up), camTrans->rotation);
    camTrans->rotation = geo_quat_norm(camTrans->rotation);
    lockCursor         = true;
  }

  input_cursor_mode_set(input, lockCursor ? InputCursorMode_Locked : InputCursorMode_Normal);
}

static bool placement_update(
    const InputManagerComp* input,
    const SceneSetEnvComp*  setEnv,
    const SceneTerrainComp* terrain,
    EcsView*                productionView,
    const GeoRay*           inputRay) {
  bool placementActive = false;
  for (EcsIterator* itr = ecs_view_itr(productionView); ecs_view_walk(itr);) {
    SceneProductionComp* production = ecs_view_write_t(itr, SceneProductionComp);
    if (!scene_product_placement_active(production)) {
      continue; // No placement active.
    }
    if (ecs_view_entity(itr) == scene_set_main(setEnv, g_sceneSetSelected)) {
      placementActive = true;

      // Update placement position.
      f32 rayT;
      if (scene_terrain_loaded(terrain)) {
        rayT = scene_terrain_intersect_ray(terrain, inputRay, g_inputInteractMaxDist);
      } else {
        rayT = geo_plane_intersect_ray(&(GeoPlane){.normal = geo_up}, inputRay);
      }
      if (rayT > g_inputInteractMinDist) {
        production->placementPos = geo_ray_position(inputRay, rayT);
      }
      if (input_triggered_lit(input, "PlacementAccept")) {
        scene_product_placement_accept(production);
      } else if (input_triggered_lit(input, "PlacementCancel")) {
        scene_product_placement_cancel(production);
      }
      if (input_triggered_lit(input, "PlacementRotateLeft")) {
        production->placementAngle -= math_pi_f32 * 0.25f;
      } else if (input_triggered_lit(input, "PlacementRotateRight")) {
        production->placementAngle += math_pi_f32 * 0.25f;
      }
    } else {
      // Not selected anymore; cancel placement.
      scene_product_placement_cancel(production);
    }
  }
  return placementActive;
}

static void select_start(GameInputComp* state, InputManagerComp* input) {
  state->selectState = InputSelectState_Down;
  state->selectStart = (GeoVector){.x = input_cursor_x(input), .y = input_cursor_y(input)};
}

static void select_start_drag(GameInputComp* state) {
  state->selectState = InputSelectState_Dragging;
}

static void select_end_click(GameInputComp* state, GameCmdComp* cmd) {
  state->selectState = InputSelectState_None;

  if (state->hoveredEntity[InputQuery_Select]) {
    switch (state->selectMode) {
    case InputSelectMode_Subtract:
      game_cmd_push_deselect(cmd, state->hoveredEntity[InputQuery_Select]);
      break;
    case InputSelectMode_Replace:
      game_cmd_push_deselect_all(cmd);
      // Fallthrough.
    case InputSelectMode_Add:
      game_cmd_push_select(cmd, state->hoveredEntity[InputQuery_Select], false /* mainObj */);
      break;
    }
  } else if (state->selectMode == InputSelectMode_Replace) {
    game_cmd_push_deselect_all(cmd);
  }
}

static void select_update_drag(
    GameInputComp*               state,
    InputManagerComp*            input,
    GameCmdComp*                 cmd,
    const SceneCollisionEnvComp* collisionEnv,
    const SceneSetEnvComp*       setEnv,
    const SceneCameraComp*       camera,
    const SceneTransformComp*    cameraTrans,
    const f32                    inputAspect) {
  const EcsEntityId oldMainObj = scene_set_main(setEnv, g_sceneSetSelected);
  if (state->selectMode == InputSelectMode_Replace) {
    game_cmd_push_deselect_all(cmd);
  }

  const GeoVector cur = {.x = input_cursor_x(input), .y = input_cursor_y(input)};
  const GeoVector min = geo_vector_min(state->selectStart, cur);
  const GeoVector max = geo_vector_max(state->selectStart, cur);
  if (min.x == max.x || min.y == max.y) {
    return;
  }
  GeoVector frustumCorners[8];
  scene_camera_frustum_corners(camera, cameraTrans, inputAspect, min, max, frustumCorners);

  const SceneQueryFilter filter = input_query_filter(input, InputQuery_Select);

  EcsEntityId results[scene_query_max_hits];
  const u32   resultCount = scene_query_frustum_all(collisionEnv, frustumCorners, &filter, results);
  for (u32 i = 0; i != resultCount; ++i) {
    if (state->selectMode == InputSelectMode_Subtract) {
      game_cmd_push_deselect(cmd, results[i]);
    } else {
      // Preserve the old main selected entity,
      const bool mainObj = results[i] == oldMainObj;
      game_cmd_push_select(cmd, results[i], mainObj);
    }
  }
}

static void select_end_drag(GameInputComp* state) { state->selectState = InputSelectState_None; }

static void input_order_attack(
    EcsWorld* world, GameCmdComp* cmd, const SceneSetEnvComp* setEnv, const EcsEntityId target) {

  // Report the attack.
  input_indicator_attack(world, target);

  // Push attack commands.
  const StringHash s = g_sceneSetSelected;
  for (const EcsEntityId* e = scene_set_begin(setEnv, s); e != scene_set_end(setEnv, s); ++e) {
    game_cmd_push_attack(cmd, *e, target);
  }
}

static void input_order_move(
    EcsWorld*              world,
    GameCmdComp*           cmdController,
    const SceneSetEnvComp* setEnv,
    const SceneNavEnvComp* nav,
    const GeoVector        targetPos) {

  // Report the move.
  input_indicator_move(world, targetPos);

  // NOTE: Always using a single normal nav layer cell per unit, so there potentially too little
  // space for large units.
  const GeoNavGrid* grid = scene_nav_grid(nav, SceneNavLayer_Normal);

  // Find unblocked cells on the nav-grid to move to.
  const u32                 selectionCount = scene_set_count(setEnv, g_sceneSetSelected);
  GeoNavCell                navCells[1024];
  const GeoNavCellContainer navCellContainer = {
      .cells    = navCells,
      .capacity = math_min(selectionCount, array_elems(navCells)),
  };
  const GeoNavCell targetNavCell = geo_nav_at_position(grid, targetPos);
  const GeoNavCond navCond       = GeoNavCond_Unblocked;
  const u32        navCellCount = geo_nav_closest_n(grid, targetNavCell, navCond, navCellContainer);

  // Push the move commands.
  const EcsEntityId* selection = scene_set_begin(setEnv, g_sceneSetSelected);
  for (u32 i = 0; i != selectionCount; ++i) {
    const EcsEntityId entity = selection[i];
    GeoVector         pos;
    if (LIKELY(i < navCellCount)) {
      const bool sameCellAsTargetPos = navCells[i].data == targetNavCell.data;
      pos = sameCellAsTargetPos ? targetPos : geo_nav_position(grid, navCells[i]);
    } else {
      // We didn't find a unblocked cell for this entity; just move to the raw targetPos.
      pos = targetPos;
    }
    game_cmd_push_move(cmdController, entity, pos);
  }
}

static void input_order_stop(GameCmdComp* cmd, const SceneSetEnvComp* setEnv) {
  const StringHash s = g_sceneSetSelected;
  for (const EcsEntityId* e = scene_set_begin(setEnv, s); e != scene_set_end(setEnv, s); ++e) {
    game_cmd_push_stop(cmd, *e);
  }
}

static void input_order(
    EcsWorld*               world,
    GameInputComp*          state,
    GameCmdComp*            cmd,
    const SceneSetEnvComp*  setEnv,
    const SceneTerrainComp* terrain,
    const SceneNavEnvComp*  nav,
    const GeoRay*           inputRay) {
  /**
   * Order an attack when clicking an opponent unit or a destructible.
   */
  if (state->hoveredEntity[InputQuery_Attack]) {
    input_order_attack(world, cmd, setEnv, state->hoveredEntity[InputQuery_Attack]);
    return;
  }
  /**
   * Order a move when clicking the terrain / ground plane.
   */
  f32 rayT = -1.0f;
  if (scene_terrain_loaded(terrain)) {
    rayT = scene_terrain_intersect_ray(terrain, inputRay, g_inputInteractMaxDist);
  } else {
    rayT = geo_plane_intersect_ray(&(GeoPlane){.normal = geo_up}, inputRay);
  }
  if (rayT > g_inputInteractMinDist) {
    const GeoVector targetPos        = geo_ray_position(inputRay, rayT);
    const GeoVector targetPosClamped = input_clamp_to_play_area(terrain, targetPos);
    input_order_move(world, cmd, setEnv, nav, targetPosClamped);
    return;
  }
}

static void input_camera_reset(GameInputComp* state, const SceneLevelManagerComp* levelManager) {
  if (scene_level_loaded(levelManager)) {
    state->camPosTgt = scene_level_startpoint(levelManager);
  } else {
    state->camPosTgt = geo_vector(0);
  }
  state->camRotYTgt = 0.0f;
  state->camZoomTgt = 0.0f;
}

static void update_camera_hover(
    GameInputComp*               state,
    InputManagerComp*            input,
    const SceneCollisionEnvComp* collisionEnv,
    const SceneTimeComp*         time,
    const GeoRay*                inputRay) {

  static const InputBlocker g_hoverBlockers =
      InputBlocker_HoveringUi | InputBlocker_PrefabCreate | InputBlocker_EntityPicker;

  const bool isBlocked = (input_blockers(input) & g_hoverBlockers) != 0;
  for (InputQueryType type = 0; type != InputQuery_Count; ++type) {
    EcsEntityId newHover = 0;
    if (!isBlocked) {
      newHover = input_query_ray(collisionEnv, input, type, inputRay);
    }
    if (newHover && state->hoveredEntity[type] == newHover) {
      state->hoveredTime[type] += time->realDelta;
    } else {
      state->hoveredEntity[type] = newHover;
      state->hoveredTime[type]   = 0;
    }
  }
}

static void update_camera_interact(
    EcsWorld*                    world,
    GameInputComp*               state,
    GameHudComp*                 hud,
    GameCmdComp*                 cmd,
    InputManagerComp*            input,
    const SceneLevelManagerComp* levelManager,
    const SceneCollisionEnvComp* collisionEnv,
    const SceneSetEnvComp*       setEnv,
    const SceneTimeComp*         time,
    const SceneTerrainComp*      terrain,
    const SceneNavEnvComp*       nav,
    const SceneCameraComp*       camera,
    const SceneTransformComp*    cameraTrans,
    EcsView*                     productionView) {
  const GeoVector inputNormPos = geo_vector(input_cursor_x(input), input_cursor_y(input));
  const f32       inputAspect  = input_cursor_aspect(input);
  const GeoRay    inputRay     = scene_camera_ray(camera, cameraTrans, inputAspect, inputNormPos);

  const bool placementActive = placement_update(input, setEnv, terrain, productionView, &inputRay);

  update_camera_hover(state, input, collisionEnv, time, &inputRay);

  state->selectMode = InputSelectMode_Replace;
  if (input_modifiers(input) & InputModifier_Shift) {
    state->selectMode = InputSelectMode_Subtract;
  } else if (input_modifiers(input) & InputModifier_Control) {
    state->selectMode = InputSelectMode_Add;
  }

  const bool         selectActive  = !placementActive && input_triggered_lit(input, "Select");
  const InputBlocker inputBlockers = InputBlocker_HoveringUi | InputBlocker_HoveringGizmo;
  switch (state->selectState) {
  case InputSelectState_None:
    if (input_blockers(input) & inputBlockers) {
      state->selectState = InputSelectState_Blocked;
    } else if (selectActive) {
      select_start(state, input);
    }
    break;
  case InputSelectState_Blocked:
    if (!(input_blockers(input) & inputBlockers)) {
      state->selectState = InputSelectState_None;
    }
    break;
  case InputSelectState_Down:
    if (selectActive) {
      if (geo_vector_mag(geo_vector_sub(inputNormPos, state->selectStart)) > g_inputDragThreshold) {
        select_start_drag(state);
      }
    } else {
      select_end_click(state, cmd);
    }
    break;
  case InputSelectState_Dragging:
    if (selectActive) {
      select_update_drag(state, input, cmd, collisionEnv, setEnv, camera, cameraTrans, inputAspect);
    } else {
      select_end_drag(state);
    }
    break;
  }

  const bool hasSelection = scene_set_count(setEnv, g_sceneSetSelected) != 0;
  if (!placementActive && !selectActive && hasSelection && input_triggered_lit(input, "Order")) {
    input_order(world, state, cmd, setEnv, terrain, nav, &inputRay);
  }
  const u32 newLevelCounter = scene_level_counter(levelManager);
  if (state->lastLevelCounter != newLevelCounter) {
    input_camera_reset(state, levelManager);
    state->flags |= InputFlags_SnapCamera;
    state->lastLevelCounter = newLevelCounter;
  }
  if (hud && game_hud_consume_action(hud, GameHudAction_CameraReset)) {
    input_camera_reset(state, levelManager);
  }
}

/**
 * Update the global collision mask to include debug colliders when we have the dev input active.
 * This allows us to use the debug colliders to select entities that have no collider.
 */
static void input_update_collision_mask(SceneCollisionEnvComp* env, const InputManagerComp* input) {
  SceneLayer ignoreMask = scene_collision_ignore_mask(env);
  if (input_layer_active(input, string_hash_lit("Dev"))) {
    ignoreMask &= ~SceneLayer_Debug; // Include debug layer.
  } else {
    ignoreMask |= SceneLayer_Debug; // Ignore debug layer;
  }
  scene_collision_ignore_mask_set(env, ignoreMask);
}

static GameInputComp* input_state_init(EcsWorld* world, const EcsEntityId windowEntity) {
  return ecs_world_add_t(
      world,
      windowEntity,
      GameInputComp,
      .uiCanvas = ui_canvas_create(world, windowEntity, UiCanvasCreateFlags_ToBack));
}

ecs_view_define(GlobalUpdateView) {
  ecs_access_read(SceneLevelManagerComp);
  ecs_access_read(SceneNavEnvComp);
  ecs_access_read(SceneSetEnvComp);
  ecs_access_read(SceneTerrainComp);
  ecs_access_read(SceneTimeComp);
  ecs_access_write(GameCmdComp);
  ecs_access_write(InputManagerComp);
  ecs_access_write(SceneCollisionEnvComp);
}

ecs_view_define(CameraView) {
  ecs_access_maybe_write(GameInputComp);
  ecs_access_maybe_write(GameHudComp);
  ecs_access_read(SceneCameraComp);
  ecs_access_write(SceneTransformComp);
}

ecs_view_define(ProductionView) { ecs_access_write(SceneProductionComp); }

ecs_system_define(GameInputUpdateSys) {
  EcsView*     globalView = ecs_world_view_t(world, GlobalUpdateView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (!globalItr) {
    return;
  }
  GameCmdComp*                 cmd          = ecs_view_write_t(globalItr, GameCmdComp);
  const SceneLevelManagerComp* levelManager = ecs_view_read_t(globalItr, SceneLevelManagerComp);
  const SceneNavEnvComp*       nav          = ecs_view_read_t(globalItr, SceneNavEnvComp);
  const SceneSetEnvComp*       setEnv       = ecs_view_read_t(globalItr, SceneSetEnvComp);
  const SceneTerrainComp*      terrain      = ecs_view_read_t(globalItr, SceneTerrainComp);
  const SceneTimeComp*         time         = ecs_view_read_t(globalItr, SceneTimeComp);
  InputManagerComp*            input        = ecs_view_write_t(globalItr, InputManagerComp);
  SceneCollisionEnvComp*       colEnv       = ecs_view_write_t(globalItr, SceneCollisionEnvComp);

  input_update_collision_mask(colEnv, input);

  EcsView* cameraView     = ecs_world_view_t(world, CameraView);
  EcsView* productionView = ecs_world_view_t(world, ProductionView);

  for (EcsIterator* camItr = ecs_view_itr(cameraView); ecs_view_walk(camItr);) {
    const SceneCameraComp* cam      = ecs_view_read_t(camItr, SceneCameraComp);
    SceneTransformComp*    camTrans = ecs_view_write_t(camItr, SceneTransformComp);
    GameInputComp*         state    = ecs_view_write_t(camItr, GameInputComp);
    GameHudComp*           hud      = ecs_view_write_t(camItr, GameHudComp);
    if (!state) {
      input_state_init(world, ecs_view_entity(camItr));
      continue;
    }
    if (hud && game_hud_consume_action(hud, GameHudAction_OrderStop)) {
      input_order_stop(cmd, setEnv);
    }

    state->lastSelectionCount = scene_set_count(setEnv, g_sceneSetSelected);

    bool windowActive = input_active_window(input) == ecs_view_entity(camItr);
    switch (state->type) {
    case GameInputType_Normal:
      update_camera_movement(state, input, time, terrain, camTrans, windowActive);
      break;
    case GameInputType_FreeCamera:
      update_camera_movement_dev(input, time, cam, camTrans);
      break;
    case GameInputType_None:
      windowActive = false;
      break;
    }

    if (windowActive) {
      update_group_input(state, cmd, input, setEnv, time);
      update_camera_interact(
          world,
          state,
          hud,
          cmd,
          input,
          levelManager,
          colEnv,
          setEnv,
          time,
          terrain,
          nav,
          cam,
          camTrans,
          productionView);
    } else {
      state->selectState = InputSelectState_None;
      mem_set(array_mem(state->hoveredEntity), 0);
      input_cursor_mode_set(input, InputCursorMode_Normal);
    }
  }
}

ecs_view_define(UiCameraView) { ecs_access_write(GameInputComp); }

ecs_view_define(UiCanvasView) {
  ecs_view_flags(EcsViewFlags_Exclusive); // Only access the canvas's we create.
  ecs_access_write(UiCanvasComp);
}

static UiInteractType input_select_ui_interaction(const InputSelectMode mode) {
  switch (mode) {
  case InputSelectMode_Replace:
    return UiInteractType_Select;
  case InputSelectMode_Add:
    return UiInteractType_SelectAdd;
  case InputSelectMode_Subtract:
    return UiInteractType_SelectSubtract;
  }
  UNREACHABLE
}

ecs_system_define(GameInputDrawUiSys) {
  EcsIterator* canvasItr  = ecs_view_itr(ecs_world_view_t(world, UiCanvasView));
  EcsView*     cameraView = ecs_world_view_t(world, UiCameraView);
  for (EcsIterator* itr = ecs_view_itr(cameraView); ecs_view_walk(itr);) {
    GameInputComp* state = ecs_view_write_t(itr, GameInputComp);
    if (!ecs_view_maybe_jump(canvasItr, state->uiCanvas)) {
      continue;
    }
    UiCanvasComp* c = ecs_view_write_t(canvasItr, UiCanvasComp);
    ui_canvas_reset(c);
    ui_canvas_to_back(c);

    switch (state->selectState) {
    case InputSelectState_None: {
      if (state->hoveredEntity[InputQuery_Select]) {
        ui_canvas_interact_type(c, input_select_ui_interaction(state->selectMode));
      } else if (state->lastSelectionCount && state->hoveredEntity[InputQuery_Attack]) {
        ui_canvas_interact_type(c, UiInteractType_Target);
      }
    } break;
    case InputSelectState_Down: {
      ui_canvas_interact_type(c, input_select_ui_interaction(state->selectMode));
    } break;
    case InputSelectState_Dragging: {
      ui_canvas_interact_type(c, input_select_ui_interaction(state->selectMode));

      const UiVector startPos = ui_vector(state->selectStart.x, state->selectStart.y);
      ui_layout_move(c, startPos, UiBase_Canvas, Ui_XY);
      ui_layout_resize_to(c, UiBase_Input, UiAlign_BottomLeft, Ui_XY);
      ui_style_color(c, ui_color(255, 255, 255, 16));
      ui_style_outline(c, 3);
      ui_canvas_draw_glyph(c, UiShape_Square, 10, UiFlags_None);
    } break;
    default:
      break;
    }
  }
}

ecs_module_init(game_input_module) {
  ecs_register_comp(GameInputComp);

  ecs_register_view(GlobalUpdateView);
  ecs_register_view(CameraView);
  ecs_register_view(UiCameraView);
  ecs_register_view(UiCanvasView);
  ecs_register_view(ProductionView);

  ecs_register_system(
      GameInputUpdateSys,
      ecs_view_id(GlobalUpdateView),
      ecs_view_id(CameraView),
      ecs_view_id(ProductionView));
  ecs_register_system(GameInputDrawUiSys, ecs_view_id(UiCameraView), ecs_view_id(UiCanvasView));

  ecs_order(GameInputUpdateSys, GameOrder_Input);
  ecs_order(GameInputDrawUiSys, GameOrder_InputUi);

  // Initialize group action hashes.
  for (u32 i = 0; i != game_cmd_group_count; ++i) {
    g_inputGroupActions[i] = string_hash(fmt_write_scratch("CommandGroup{}", fmt_int(i + 1)));
  }
}

GameInputType game_input_type(const GameInputComp* comp) { return comp->type; }
void game_input_type_set(GameInputComp* comp, const GameInputType type) { comp->type = type; }

void game_input_camera_center(GameInputComp* state, const GeoVector worldPos) {
  state->camPosTgt = worldPos;
}

void game_input_set_allow_zoom_over_ui(GameInputComp* state, const bool allowZoomOverUI) {
  if (allowZoomOverUI) {
    state->flags |= InputFlags_AllowZoomOverUi;
  } else {
    state->flags &= ~InputFlags_AllowZoomOverUi;
  }
}

bool game_input_hovered_entity(
    const GameInputComp* state, EcsEntityId* outEntity, TimeDuration* outTime) {
  if (state->selectState >= InputSelectState_Down) {
    return false; // Disallow hovering UI when actively selecting a unit.
  }
  for (InputQueryType type = 0; type != InputQuery_Count; ++type) {
    if (state->hoveredEntity[type]) {
      *outEntity = state->hoveredEntity[type];
      *outTime   = state->hoveredTime[type];
      return true;
    }
  }
  return false;
}
