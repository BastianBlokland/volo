#include "core_math.h"
#include "ecs_world.h"
#include "geo_plane.h"
#include "input_manager.h"
#include "scene_camera.h"
#include "scene_collision.h"
#include "scene_selection.h"
#include "scene_time.h"
#include "scene_transform.h"
#include "ui.h"

#include "cmd_internal.h"

static const f32 g_inputMinInteractDist       = 1.0f;
static const f32 g_inputMaxInteractDist       = 250.0f;
static const f32 g_inputCamMoveSpeed          = 10.0f;
static const f32 g_inputCamMoveSpeedBoostMult = 4.0f;
static const f32 g_inputCamRotateSensitivity  = 2.0f;
static const f32 g_inputDragThreshold         = 0.005f; // In normalized screen-space coordinates.

typedef enum {
  InputSelectState_None,
  InputSelectState_Down,
  InputSelectState_Dragging,
} InputSelectState;

ecs_comp_define(InputStateComp) {
  EcsEntityId      uiCanvas;
  InputSelectState selectState;
  f32              selectStart[2]; // NOTE: Normalized screen-space coordinates.
};

static void update_camera_movement(
    const SceneTimeComp*      time,
    const InputManagerComp*   input,
    const SceneSelectionComp* selection,
    const SceneCameraComp*    camera,
    SceneTransformComp*       camTrans) {

  f32 moveDelta = scene_real_delta_seconds(time) * g_inputCamMoveSpeed;
  if (input_triggered_lit(input, "CameraMoveBoost")) {
    moveDelta *= g_inputCamMoveSpeedBoostMult;
  }
  const GeoVector right   = geo_quat_rotate(camTrans->rotation, geo_right);
  const GeoVector up      = geo_quat_rotate(camTrans->rotation, geo_up);
  const GeoVector forward = geo_quat_rotate(camTrans->rotation, geo_forward);

  if (input_triggered_lit(input, "CameraMoveForward")) {
    const GeoVector dir = camera->flags & SceneCameraFlags_Orthographic ? up : forward;
    camTrans->position  = geo_vector_add(camTrans->position, geo_vector_mul(dir, moveDelta));
  }
  if (input_triggered_lit(input, "CameraMoveBackward")) {
    const GeoVector dir = camera->flags & SceneCameraFlags_Orthographic ? up : forward;
    camTrans->position  = geo_vector_sub(camTrans->position, geo_vector_mul(dir, moveDelta));
  }
  if (input_triggered_lit(input, "CameraMoveRight")) {
    camTrans->position = geo_vector_add(camTrans->position, geo_vector_mul(right, moveDelta));
  }
  if (input_triggered_lit(input, "CameraMoveLeft")) {
    camTrans->position = geo_vector_sub(camTrans->position, geo_vector_mul(right, moveDelta));
  }

  const bool hasSelection = !scene_selection_empty(selection);
  const bool cursorLocked = input_cursor_mode(input) == InputCursorMode_Locked;
  if ((input_triggered_lit(input, "CameraLookEnable") && !hasSelection) || cursorLocked) {
    const f32 deltaX = input_cursor_delta_x(input) * g_inputCamRotateSensitivity;
    const f32 deltaY = input_cursor_delta_y(input) * -g_inputCamRotateSensitivity;

    camTrans->rotation = geo_quat_mul(geo_quat_angle_axis(right, deltaY), camTrans->rotation);
    camTrans->rotation = geo_quat_mul(geo_quat_angle_axis(geo_up, deltaX), camTrans->rotation);
    camTrans->rotation = geo_quat_norm(camTrans->rotation);
  }
}

static void select_start(InputStateComp* state, InputManagerComp* input) {
  state->selectState    = InputSelectState_Down;
  state->selectStart[0] = input_cursor_x(input);
  state->selectStart[1] = input_cursor_y(input);
}

static void select_start_drag(InputStateComp* state) {
  state->selectState = InputSelectState_Dragging;
}

static void select_end_click(
    InputStateComp*              state,
    CmdControllerComp*           cmdController,
    const SceneCollisionEnvComp* collisionEnv,
    const SceneSelectionComp*    sel,
    const GeoRay*                inputRay) {
  state->selectState = InputSelectState_None;

  SceneRayHit hit;
  const bool  hasHit = scene_query_ray(collisionEnv, inputRay, &hit);

  if (hasHit && !scene_selection_contains(sel, hit.entity)) {
    cmd_push_select(cmdController, hit.entity);
  } else {
    cmd_push_deselect(cmdController);
  }
}

static void select_end_drag(InputStateComp* state) { state->selectState = InputSelectState_None; }

static void update_camera_interact(
    InputStateComp*              state,
    CmdControllerComp*           cmdController,
    InputManagerComp*            input,
    const SceneCollisionEnvComp* collisionEnv,
    const SceneSelectionComp*    sel,
    const SceneCameraComp*       camera,
    const SceneTransformComp*    cameraTrans) {
  const GeoVector inputNormPos = geo_vector(input_cursor_x(input), input_cursor_y(input));
  const f32       inputAspect  = input_cursor_aspect(input);
  const GeoRay    inputRay     = scene_camera_ray(camera, cameraTrans, inputAspect, inputNormPos);
  const GeoPlane  groundPlane  = {.normal = geo_up};

  const bool selectActive = input_triggered_lit(input, "Select");
  switch (state->selectState) {
  case InputSelectState_None:
    if (selectActive) {
      select_start(state, input);
    }
    break;
  case InputSelectState_Down:
    if (selectActive) {
      const f32 x = input_cursor_x(input), y = input_cursor_y(input);
      const f32 deltaX = x - state->selectStart[0], deltaY = y - state->selectStart[1];
      if (math_abs(deltaX) > g_inputDragThreshold || math_abs(deltaY) > g_inputDragThreshold) {
        select_start_drag(state);
      }
    } else {
      select_end_click(state, cmdController, collisionEnv, sel, &inputRay);
    }
    break;
  case InputSelectState_Dragging:
    if (!selectActive) {
      select_end_drag(state);
    }
    break;
  }

  if (!selectActive && input_triggered_lit(input, "Order")) {
    const f32 rayT = geo_plane_intersect_ray(&groundPlane, &inputRay);
    if (rayT > g_inputMinInteractDist && rayT < g_inputMaxInteractDist) {
      const GeoVector targetPos = geo_ray_position(&inputRay, rayT);
      for (const EcsEntityId* e = scene_selection_begin(sel); e != scene_selection_end(sel); ++e) {
        cmd_push_move(cmdController, *e, targetPos);
      }
    }
  }

  if (!selectActive && input_triggered_lit(input, "SpawnUnit")) {
    const f32 rayT = geo_plane_intersect_ray(&groundPlane, &inputRay);
    if (rayT > g_inputMinInteractDist && rayT < g_inputMaxInteractDist) {
      cmd_push_spawn_unit(cmdController, geo_ray_position(&inputRay, rayT));
    }
  }
  if (!selectActive && input_triggered_lit(input, "SpawnWall")) {
    const f32 rayT = geo_plane_intersect_ray(&groundPlane, &inputRay);
    if (rayT > g_inputMinInteractDist && rayT < g_inputMaxInteractDist) {
      cmd_push_spawn_wall(cmdController, geo_ray_position(&inputRay, rayT));
    }
  }

  if (!selectActive && input_triggered_lit(input, "CursorLock")) {
    input_cursor_mode_set(input, input_cursor_mode(input) ^ 1);
  }
}

static void input_state_init(EcsWorld* world, const EcsEntityId windowEntity) {
  ecs_world_add_t(
      world,
      windowEntity,
      InputStateComp,
      .uiCanvas = ui_canvas_create(world, windowEntity, UiCanvasCreateFlags_ToBack));
}

ecs_view_define(GlobalUpdateView) {
  ecs_access_read(SceneCollisionEnvComp);
  ecs_access_read(SceneSelectionComp);
  ecs_access_read(SceneTimeComp);
  ecs_access_write(CmdControllerComp);
  ecs_access_write(InputManagerComp);
}

ecs_view_define(CameraView) {
  ecs_access_maybe_write(InputStateComp);
  ecs_access_read(SceneCameraComp);
  ecs_access_write(SceneTransformComp);
}

ecs_system_define(InputUpdateSys) {
  EcsView*     globalView = ecs_world_view_t(world, GlobalUpdateView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (!globalItr) {
    return;
  }
  CmdControllerComp*           cmdController = ecs_view_write_t(globalItr, CmdControllerComp);
  const SceneCollisionEnvComp* collisionEnv  = ecs_view_read_t(globalItr, SceneCollisionEnvComp);
  const SceneSelectionComp*    sel           = ecs_view_read_t(globalItr, SceneSelectionComp);
  const SceneTimeComp*         time          = ecs_view_read_t(globalItr, SceneTimeComp);
  InputManagerComp*            input         = ecs_view_write_t(globalItr, InputManagerComp);

  if (input_triggered_lit(input, "Destroy")) {
    for (const EcsEntityId* e = scene_selection_begin(sel); e != scene_selection_end(sel); ++e) {
      cmd_push_destroy(cmdController, *e);
    }
  }

  EcsView* cameraView = ecs_world_view_t(world, CameraView);
  for (EcsIterator* itr = ecs_view_itr(cameraView); ecs_view_walk(itr);) {
    EcsIterator*           camItr      = ecs_view_at(cameraView, ecs_view_entity(itr));
    const SceneCameraComp* camera      = ecs_view_read_t(camItr, SceneCameraComp);
    SceneTransformComp*    cameraTrans = ecs_view_write_t(camItr, SceneTransformComp);
    InputStateComp*        state       = ecs_view_write_t(camItr, InputStateComp);
    if (!state) {
      input_state_init(world, ecs_view_entity(camItr));
      continue;
    }
    if (input_active_window(input) == ecs_view_entity(itr)) {
      update_camera_movement(time, input, sel, camera, cameraTrans);
      update_camera_interact(state, cmdController, input, collisionEnv, sel, camera, cameraTrans);
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

    if (state->selectState == InputSelectState_Dragging) {
      const UiVector start = {.x = state->selectStart[0], .y = state->selectStart[1]};
      ui_layout_move(canvas, start, UiBase_Canvas, Ui_XY);
      ui_layout_resize_to(canvas, UiBase_Input, UiAlign_BottomLeft, Ui_XY);
      ui_canvas_draw_glyph(canvas, UiShape_Square, 0, UiFlags_None);
    }
  }
}

ecs_module_init(sandbox_input_module) {
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
