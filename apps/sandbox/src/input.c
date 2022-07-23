#include "ecs_world.h"
#include "geo_plane.h"
#include "input_manager.h"
#include "scene_camera.h"
#include "scene_collision.h"
#include "scene_selection.h"
#include "scene_time.h"
#include "scene_transform.h"

#include "cmd_internal.h"

static const f32 g_inputMinSpawnDist          = 1.0f;
static const f32 g_inputMaxSpawnDist          = 100.0f;
static const f32 g_inputCamMoveSpeed          = 10.0f;
static const f32 g_inputCamMoveSpeedBoostMult = 4.0f;
static const f32 g_inputCamRotateSensitivity  = 2.0f;

ecs_view_define(GlobalUpdateView) {
  ecs_access_read(SceneCollisionEnvComp);
  ecs_access_read(SceneSelectionComp);
  ecs_access_read(SceneTimeComp);
  ecs_access_write(CmdControllerComp);
  ecs_access_write(InputManagerComp);
}

ecs_view_define(CameraView) {
  ecs_access_read(SceneCameraComp);
  ecs_access_write(SceneTransformComp);
}

static void update_camera_movement(
    const SceneTimeComp*    time,
    const InputManagerComp* input,
    const SceneCameraComp*  camera,
    SceneTransformComp*     camTrans) {

  f32 moveDelta = scene_delta_seconds(time) * g_inputCamMoveSpeed;
  if (input_triggered_lit(input, "SandboxCameraMoveBoost")) {
    moveDelta *= g_inputCamMoveSpeedBoostMult;
  }
  const GeoVector right   = geo_quat_rotate(camTrans->rotation, geo_right);
  const GeoVector up      = geo_quat_rotate(camTrans->rotation, geo_up);
  const GeoVector forward = geo_quat_rotate(camTrans->rotation, geo_forward);

  if (input_triggered_lit(input, "SandboxCameraMoveForward")) {
    const GeoVector dir = camera->flags & SceneCameraFlags_Orthographic ? up : forward;
    camTrans->position  = geo_vector_add(camTrans->position, geo_vector_mul(dir, moveDelta));
  }
  if (input_triggered_lit(input, "SandboxCameraMoveBackward")) {
    const GeoVector dir = camera->flags & SceneCameraFlags_Orthographic ? up : forward;
    camTrans->position  = geo_vector_sub(camTrans->position, geo_vector_mul(dir, moveDelta));
  }
  if (input_triggered_lit(input, "SandboxCameraMoveRight")) {
    camTrans->position = geo_vector_add(camTrans->position, geo_vector_mul(right, moveDelta));
  }
  if (input_triggered_lit(input, "SandboxCameraMoveLeft")) {
    camTrans->position = geo_vector_sub(camTrans->position, geo_vector_mul(right, moveDelta));
  }

  const bool cursorLocked = input_cursor_mode(input) == InputCursorMode_Locked;
  if (input_triggered_lit(input, "SandboxCameraLookEnable") || cursorLocked) {
    const f32 deltaX = input_cursor_delta_x(input) * g_inputCamRotateSensitivity;
    const f32 deltaY = input_cursor_delta_y(input) * -g_inputCamRotateSensitivity;

    camTrans->rotation = geo_quat_mul(geo_quat_angle_axis(right, deltaY), camTrans->rotation);
    camTrans->rotation = geo_quat_mul(geo_quat_angle_axis(geo_up, deltaX), camTrans->rotation);
    camTrans->rotation = geo_quat_norm(camTrans->rotation);
  }
}

static void update_camera_interact(
    CmdControllerComp*           cmdController,
    InputManagerComp*            input,
    const SceneCollisionEnvComp* collisionEnv,
    const SceneSelectionComp*    selection,
    const SceneCameraComp*       camera,
    const SceneTransformComp*    cameraTrans) {

  const GeoVector inputNormPos = geo_vector(input_cursor_x(input), input_cursor_y(input));
  const f32       inputAspect  = input_cursor_aspect(input);
  const GeoRay    inputRay     = scene_camera_ray(camera, cameraTrans, inputAspect, inputNormPos);
  const GeoPlane  groundPlane  = {.normal = geo_up};

  if (input_triggered_lit(input, "SandboxSelect")) {
    SceneRayHit hit;
    const bool  hasHit = scene_query_ray(collisionEnv, &inputRay, &hit);

    if (hasHit && hit.entity != scene_selected(selection)) {
      cmd_push_select(cmdController, hit.entity);
    } else {
      cmd_push_deselect(cmdController);
    }
  }

  if (input_triggered_lit(input, "SandboxCursorLock")) {
    input_cursor_mode_set(input, input_cursor_mode(input) ^ 1);
  }

  if (input_triggered_lit(input, "SandboxSpawn")) {
    const f32 rayT = geo_plane_intersect_ray(&groundPlane, &inputRay);
    if (rayT > g_inputMinSpawnDist && rayT < g_inputMaxSpawnDist) {
      cmd_push_spawn_unit(cmdController, geo_ray_position(&inputRay, rayT));
    }
  }
}

ecs_system_define(InputUpdateSys) {
  EcsView*     globalView = ecs_world_view_t(world, GlobalUpdateView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (!globalItr) {
    return;
  }
  CmdControllerComp*           cmdController = ecs_view_write_t(globalItr, CmdControllerComp);
  const SceneCollisionEnvComp* collisionEnv  = ecs_view_read_t(globalItr, SceneCollisionEnvComp);
  const SceneSelectionComp*    selection     = ecs_view_read_t(globalItr, SceneSelectionComp);
  const SceneTimeComp*         time          = ecs_view_read_t(globalItr, SceneTimeComp);
  InputManagerComp*            input         = ecs_view_write_t(globalItr, InputManagerComp);

  EcsView* cameraView = ecs_world_view_t(world, CameraView);
  if (ecs_view_contains(cameraView, input_active_window(input))) {
    EcsIterator*           camItr      = ecs_view_at(cameraView, input_active_window(input));
    const SceneCameraComp* camera      = ecs_view_read_t(camItr, SceneCameraComp);
    SceneTransformComp*    cameraTrans = ecs_view_write_t(camItr, SceneTransformComp);
    update_camera_movement(time, input, camera, cameraTrans);
    update_camera_interact(cmdController, input, collisionEnv, selection, camera, cameraTrans);
  }
}

ecs_module_init(sandbox_input_module) {
  ecs_register_view(GlobalUpdateView);
  ecs_register_view(CameraView);

  ecs_register_system(InputUpdateSys, ecs_view_id(GlobalUpdateView), ecs_view_id(CameraView));
}
