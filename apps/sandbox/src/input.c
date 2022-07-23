#include "ecs_world.h"
#include "geo_plane.h"
#include "input_manager.h"
#include "scene_camera.h"
#include "scene_collision.h"
#include "scene_selection.h"

#include "cmd_internal.h"

static const f32 g_inputMinSpawnDist = 1.0f;
static const f32 g_inputMaxSpawnDist = 100.0f;

ecs_view_define(GlobalUpdateView) {
  ecs_access_read(InputManagerComp);
  ecs_access_read(SceneCollisionEnvComp);
  ecs_access_read(SceneSelectionComp);
  ecs_access_write(CmdControllerComp);
}

ecs_view_define(CameraView) {
  ecs_access_read(SceneCameraComp);
  ecs_access_read(SceneTransformComp);
}

static void input_update_camera(
    CmdControllerComp*           cmdController,
    const InputManagerComp*      input,
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
  const InputManagerComp*      input         = ecs_view_read_t(globalItr, InputManagerComp);
  const SceneCollisionEnvComp* collisionEnv  = ecs_view_read_t(globalItr, SceneCollisionEnvComp);
  const SceneSelectionComp*    selection     = ecs_view_read_t(globalItr, SceneSelectionComp);

  EcsView* cameraView = ecs_world_view_t(world, CameraView);
  if (ecs_view_contains(cameraView, input_active_window(input))) {
    EcsIterator*              camItr      = ecs_view_at(cameraView, input_active_window(input));
    const SceneCameraComp*    camera      = ecs_view_read_t(camItr, SceneCameraComp);
    const SceneTransformComp* cameraTrans = ecs_view_read_t(camItr, SceneTransformComp);
    input_update_camera(cmdController, input, collisionEnv, selection, camera, cameraTrans);
  }
}

ecs_module_init(sandbox_input_module) {
  ecs_register_view(GlobalUpdateView);
  ecs_register_view(CameraView);

  ecs_register_system(InputUpdateSys, ecs_view_id(GlobalUpdateView), ecs_view_id(CameraView));
}
