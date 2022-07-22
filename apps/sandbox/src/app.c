#include "app_ecs.h"
#include "asset.h"
#include "debug.h"
#include "ecs.h"
#include "gap.h"
#include "input.h"
#include "rend_register.h"
#include "scene_camera.h"
#include "scene_collision.h"
#include "scene_register.h"
#include "scene_selection.h"
#include "ui_register.h"

#include "unit_internal.h"

static const GapVector g_windowSize = {1920, 1080};

ecs_comp_define(AppComp) { bool unitSpawned; };

ecs_view_define(GlobalUpdateView) {
  ecs_access_read(InputManagerComp);
  ecs_access_read(SceneCollisionEnvComp);
  ecs_access_read(UnitDatabaseComp);
  ecs_access_write(AppComp);
  ecs_access_write(SceneSelectionComp);
}

ecs_view_define(CameraView) {
  ecs_access_write(SceneCameraComp);
  ecs_access_write(SceneTransformComp);
}

ecs_view_define(WindowExistenceView) { ecs_access_with(GapWindowComp); }

ecs_system_define(AppUpdateSys) {
  EcsView*     globalView = ecs_world_view_t(world, GlobalUpdateView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (!globalItr) {
    return;
  }
  const UnitDatabaseComp*      unitDb       = ecs_view_read_t(globalItr, UnitDatabaseComp);
  AppComp*                     app          = ecs_view_write_t(globalItr, AppComp);
  const InputManagerComp*      input        = ecs_view_read_t(globalItr, InputManagerComp);
  const SceneCollisionEnvComp* collisionEnv = ecs_view_read_t(globalItr, SceneCollisionEnvComp);
  SceneSelectionComp*          selection    = ecs_view_write_t(globalItr, SceneSelectionComp);

  if (!app->unitSpawned) {
    unit_spawn(world, unitDb, geo_vector(0));
    app->unitSpawned = true;
  }

  const EcsEntityId activeWindow = input_active_window(input);
  EcsIterator*      camItr = ecs_view_maybe_at(ecs_world_view_t(world, CameraView), activeWindow);
  if (camItr && input_triggered_lit(input, "SandboxSelect")) {
    const SceneCameraComp*    cam     = ecs_view_read_t(camItr, SceneCameraComp);
    const SceneTransformComp* trans   = ecs_view_read_t(camItr, SceneTransformComp);
    const GeoVector           normPos = geo_vector(input_cursor_x(input), input_cursor_y(input));
    const GeoRay ray = scene_camera_ray(cam, trans, input_cursor_aspect(input), normPos);

    SceneRayHit hit;
    if (scene_query_ray(collisionEnv, &ray, &hit) && hit.entity != scene_selected(selection)) {
      scene_select(selection, hit.entity);
    } else {
      scene_deselect(selection);
    }
  }
}

ecs_module_init(sandbox_app_module) {
  ecs_register_comp(AppComp);

  ecs_register_view(GlobalUpdateView);
  ecs_register_view(CameraView);
  ecs_register_view(WindowExistenceView);

  ecs_register_system(AppUpdateSys, ecs_view_id(GlobalUpdateView), ecs_view_id(CameraView));
}

static CliId g_assetFlag;

void app_ecs_configure(CliApp* app) {
  cli_app_register_desc(app, string_lit("Volo Sandbox Application"));

  g_assetFlag = cli_register_flag(app, 'a', string_lit("assets"), CliOptionFlags_Required);
  cli_register_desc(app, g_assetFlag, string_lit("Path to asset directory."));
}

void app_ecs_register(EcsDef* def, MAYBE_UNUSED const CliInvocation* invoc) {
  asset_register(def);
  debug_register(def);
  gap_register(def);
  input_register(def);
  rend_register(def);
  scene_register(def);
  ui_register(def);

  ecs_register_module(def, sandbox_app_module);
  ecs_register_module(def, sandbox_unit_module);
}

void app_ecs_init(EcsWorld* world, const CliInvocation* invoc) {
  const String assetPath = cli_read_string(invoc, g_assetFlag, string_empty);

  asset_manager_create_fs(
      world, AssetManagerFlags_TrackChanges | AssetManagerFlags_DelayUnload, assetPath);

  const EcsEntityId window = gap_window_create(world, GapWindowFlags_Default, g_windowSize);
  debug_menu_create(world, window);

  ecs_world_add_t(world, ecs_world_global(world), AppComp);
}

bool app_ecs_should_quit(EcsWorld* world) { return !ecs_utils_any(world, WindowExistenceView); }
