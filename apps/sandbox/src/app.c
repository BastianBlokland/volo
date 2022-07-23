#include "app_ecs.h"
#include "asset.h"
#include "core_math.h"
#include "debug.h"
#include "ecs.h"
#include "gap.h"
#include "input.h"
#include "rend_register.h"
#include "scene_camera.h"
#include "scene_register.h"
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
      .persFov   = 60 * math_deg_to_rad,
      .persNear  = 1,
      .orthoSize = 5);

  ecs_world_add_t(
      world,
      window,
      SceneTransformComp,
      .position = {0, 1.2f, -3.5f},
      .rotation = geo_quat_angle_axis(geo_right, 5 * math_deg_to_rad));
}

ecs_view_define(GlobalUpdateView) { ecs_access_read(InputManagerComp); }
ecs_view_define(WindowView) { ecs_access_write(GapWindowComp); }

ecs_system_define(AppUpdateSys) {
  EcsView*     globalView = ecs_world_view_t(world, GlobalUpdateView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (!globalItr) {
    return;
  }
  const InputManagerComp* input = ecs_view_read_t(globalItr, InputManagerComp);

  if (input_triggered_lit(input, "SandboxWindowNew")) {
    app_window_create(world);
  }
  if (input_triggered_lit(input, "SandboxWindowClose")) {
    const EcsEntityId winEntity = input_active_window(input);
    GapWindowComp*    win       = ecs_utils_write_t(world, WindowView, winEntity, GapWindowComp);
    gap_window_close(win);
  }
}

ecs_module_init(sandbox_app_module) {
  ecs_register_view(GlobalUpdateView);
  ecs_register_view(WindowView);

  ecs_register_system(AppUpdateSys, ecs_view_id(GlobalUpdateView), ecs_view_id(WindowView));
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
  const String assetPath = cli_read_string(invoc, g_assetFlag, string_empty);

  asset_manager_create_fs(
      world, AssetManagerFlags_TrackChanges | AssetManagerFlags_DelayUnload, assetPath);

  app_window_create(world);
}

bool app_ecs_should_quit(EcsWorld* world) { return !ecs_utils_any(world, WindowView); }
