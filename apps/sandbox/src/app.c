#include "app_ecs.h"
#include "asset.h"
#include "debug.h"
#include "ecs.h"
#include "gap.h"
#include "input.h"
#include "rend_register.h"
#include "scene_register.h"
#include "ui_register.h"

#include "cmd_internal.h"

static const GapVector g_windowSize = {1920, 1080};

ecs_view_define(WindowExistenceView) { ecs_access_with(GapWindowComp); }
ecs_module_init(sandbox_app_module) { ecs_register_view(WindowExistenceView); }

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

  const EcsEntityId window = gap_window_create(world, GapWindowFlags_Default, g_windowSize);
  debug_menu_create(world, window);
}

bool app_ecs_should_quit(EcsWorld* world) { return !ecs_utils_any(world, WindowExistenceView); }
