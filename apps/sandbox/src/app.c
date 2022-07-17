#include "asset.h"
#include "debug.h"
#include "ecs.h"
#include "gap.h"
#include "input.h"
#include "rend_register.h"
#include "scene_camera.h"
#include "scene_collision.h"
#include "scene_register.h"
#include "scene_renderable.h"
#include "scene_selection.h"
#include "scene_transform.h"
#include "ui_register.h"

static const GapVector g_windowSize  = {1920, 1080};
static const String    g_unitGraphic = string_static("graphics/sandbox/vanguard.gra");

ecs_comp_define(AppComp) { bool unitSpawned; };

ecs_view_define(GlobalUpdateView) {
  ecs_access_read(InputManagerComp);
  ecs_access_read(SceneCollisionEnvComp);
  ecs_access_write(AssetManagerComp);
  ecs_access_write(AppComp);
  ecs_access_write(SceneSelectionComp);
}

ecs_view_define(CameraView) {
  ecs_access_write(SceneCameraComp);
  ecs_access_write(SceneTransformComp);
}

ecs_view_define(WindowExistenceView) { ecs_access_with(GapWindowComp); }

static void spawn_unit(EcsWorld* world, AssetManagerComp* assets, const GeoVector position) {
  const EcsEntityId e       = ecs_world_entity_create(world);
  const EcsEntityId graphic = asset_lookup(world, assets, g_unitGraphic);

  ecs_world_add_t(world, e, SceneRenderableComp, .graphic = graphic);
  ecs_world_add_t(world, e, SceneTransformComp, .position = position, .rotation = geo_quat_ident);
  scene_collision_add_capsule(
      world,
      e,
      (SceneCollisionCapsule){
          .offset = {0, 0.3f, 0},
          .radius = 0.3f,
          .height = 1.2f,
      });
}

ecs_system_define(AppUpdateSys) {
  EcsView*     globalView = ecs_world_view_t(world, GlobalUpdateView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (!globalItr) {
    return;
  }
  AssetManagerComp*            assets       = ecs_view_write_t(globalItr, AssetManagerComp);
  AppComp*                     app          = ecs_view_write_t(globalItr, AppComp);
  const InputManagerComp*      input        = ecs_view_read_t(globalItr, InputManagerComp);
  const SceneCollisionEnvComp* collisionEnv = ecs_view_read_t(globalItr, SceneCollisionEnvComp);
  SceneSelectionComp*          selection    = ecs_view_write_t(globalItr, SceneSelectionComp);

  if (!app->unitSpawned) {
    spawn_unit(world, assets, geo_vector(0));
    app->unitSpawned = true;
  }

  const EcsEntityId activeWindow = input_active_window(input);
  EcsIterator*      camItr = ecs_view_maybe_at(ecs_world_view_t(world, CameraView), activeWindow);
  if (camItr && input_triggered_lit(input, "PedestalSelect")) {
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

ecs_module_init(app_module) {
  ecs_register_comp(AppComp);

  ecs_register_view(GlobalUpdateView);
  ecs_register_view(CameraView);
  ecs_register_view(WindowExistenceView);

  ecs_register_system(AppUpdateSys, ecs_view_id(GlobalUpdateView), ecs_view_id(CameraView));
}

void app_register(EcsDef* def) {
  asset_register(def);
  debug_register(def);
  gap_register(def);
  input_register(def);
  rend_register(def);
  scene_register(def);
  ui_register(def);

  ecs_register_module(def, app_module);
}

void app_init(EcsWorld* world, const String assetPath) {
  asset_manager_create_fs(
      world, AssetManagerFlags_TrackChanges | AssetManagerFlags_DelayUnload, assetPath);

  const EcsEntityId window = gap_window_create(world, GapWindowFlags_Default, g_windowSize);
  debug_menu_create(world, window);

  ecs_world_add_t(world, ecs_world_global(world), AppComp);
}

bool app_should_close(EcsWorld* world) { return !ecs_utils_any(world, WindowExistenceView); }
