#include "app_ecs.h"
#include "asset.h"
#include "core_math.h"
#include "debug.h"
#include "ecs.h"
#include "gap.h"
#include "input.h"
#include "input_resource.h"
#include "rend_register.h"
#include "scene_camera.h"
#include "scene_register.h"
#include "scene_renderable.h"
#include "scene_transform.h"
#include "ui_register.h"

#include "cmd_internal.h"

static const GapVector g_windowSize = {1920, 1080};

static void app_window_create(EcsWorld* world) {
  const EcsEntityId window = gap_window_create(world, GapWindowFlags_Default, g_windowSize);
  debug_menu_create(world, window);

  ecs_world_add_t(
      world,
      window,
      SceneCameraComp,
      .persFov   = 50 * math_deg_to_rad,
      .persNear  = 0.75f,
      .orthoSize = 5);

  ecs_world_add_t(
      world,
      window,
      SceneTransformComp,
      .position = {0, 20.0f, 0},
      .rotation = geo_quat_angle_axis(geo_right, 45 * math_deg_to_rad));
}

static void app_window_fullscreen_toggle(GapWindowComp* win) {
  const bool isFullscreen = gap_window_mode(win) == GapWindowMode_Fullscreen;
  gap_window_resize(
      win,
      isFullscreen ? gap_window_param(win, GapParam_WindowSizePreFullscreen) : gap_vector(0, 0),
      isFullscreen ? GapWindowMode_Windowed : GapWindowMode_Fullscreen);
}

static void app_scene_create_sky(EcsWorld* world, AssetManagerComp* assets) {
  const EcsEntityId entity = ecs_world_entity_create(world);
  ecs_world_add_t(
      world,
      entity,
      SceneRenderableComp,
      .graphic = asset_lookup(world, assets, string_lit("graphics/scene/sky.gra")));
  ecs_world_add_t(world, entity, SceneTagComp, .tags = SceneTags_Background);
}

ecs_comp_define(AppComp) { bool sceneCreated; };

ecs_view_define(AppUpdateGlobalView) {
  ecs_access_write(AppComp);
  ecs_access_write(AssetManagerComp);
  ecs_access_read(InputManagerComp);
}

ecs_view_define(WindowView) { ecs_access_write(GapWindowComp); }

ecs_system_define(AppUpdateSys) {
  EcsView*     globalView = ecs_world_view_t(world, AppUpdateGlobalView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (!globalItr) {
    return;
  }
  AppComp*                app    = ecs_view_write_t(globalItr, AppComp);
  AssetManagerComp*       assets = ecs_view_write_t(globalItr, AssetManagerComp);
  const InputManagerComp* input  = ecs_view_read_t(globalItr, InputManagerComp);

  if (!app->sceneCreated) {
    app_scene_create_sky(world, assets);
    app->sceneCreated = true;
  }

  if (input_triggered_lit(input, "WindowNew")) {
    app_window_create(world);
  }

  EcsView*     windowView      = ecs_world_view_t(world, WindowView);
  EcsIterator* activeWindowItr = ecs_view_maybe_at(windowView, input_active_window(input));
  if (activeWindowItr) {
    GapWindowComp* win = ecs_view_write_t(activeWindowItr, GapWindowComp);
    if (input_triggered_lit(input, "WindowClose")) {
      gap_window_close(win);
    }
    if (input_triggered_lit(input, "WindowFullscreen")) {
      app_window_fullscreen_toggle(win);
    }
  }
}

ecs_module_init(sandbox_app_module) {
  ecs_register_comp(AppComp);

  ecs_register_view(AppUpdateGlobalView);
  ecs_register_view(WindowView);

  ecs_register_system(AppUpdateSys, ecs_view_id(AppUpdateGlobalView), ecs_view_id(WindowView));
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
  ecs_register_module(def, sandbox_cmd_module);
  ecs_register_module(def, sandbox_input_module);
  ecs_register_module(def, sandbox_unit_module);
}

void app_ecs_init(EcsWorld* world, const CliInvocation* invoc) {
  ecs_world_add_t(world, ecs_world_global(world), AppComp);

  const String assetPath = cli_read_string(invoc, g_assetFlag, string_empty);
  asset_manager_create_fs(
      world, AssetManagerFlags_TrackChanges | AssetManagerFlags_DelayUnload, assetPath);

  input_resource_create(world, string_lit("input/sandbox.imp"));

  app_window_create(world);
}

bool app_ecs_should_quit(EcsWorld* world) { return !ecs_utils_any(world, WindowView); }
