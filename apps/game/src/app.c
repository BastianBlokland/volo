#include "app_ecs.h"
#include "asset.h"
#include "core_file.h"
#include "core_math.h"
#include "debug.h"
#include "ecs.h"
#include "gap.h"
#include "input.h"
#include "input_resource.h"
#include "log_logger.h"
#include "rend_register.h"
#include "scene_camera.h"
#include "scene_level.h"
#include "scene_prefab.h"
#include "scene_register.h"
#include "scene_sound.h"
#include "scene_terrain.h"
#include "scene_transform.h"
#include "scene_weapon.h"
#include "snd_register.h"
#include "ui_register.h"
#include "vfx_register.h"

#include "cmd_internal.h"

static const GapVector g_appWindowSize = {1920, 1080};

ecs_comp_define(AppWindowComp) { EcsEntityId debugMenu; };

static void app_ambiance_create(EcsWorld* world, AssetManagerComp* assets) {
  const EcsEntityId e = ecs_world_entity_create(world);
  ecs_world_add_t(
      world,
      e,
      SceneSoundComp,
      .asset   = asset_lookup(world, assets, string_lit("external/sound/ambiance-01.wav")),
      .pitch   = 1.0f,
      .gain    = 0.4f,
      .looping = true);
}

static void app_window_create(EcsWorld* world) {
  const EcsEntityId window    = gap_window_create(world, GapWindowFlags_Default, g_appWindowSize);
  const EcsEntityId debugMenu = debug_menu_create(world, window);
  ecs_world_add_t(world, window, AppWindowComp, .debugMenu = debugMenu);

  ecs_world_add_t(
      world,
      window,
      SceneCameraComp,
      .persFov   = 50 * math_deg_to_rad,
      .persNear  = 0.75f,
      .orthoSize = 5);

  ecs_world_add_empty_t(world, window, SceneSoundListenerComp);

  ecs_world_add_t(world, window, SceneTransformComp, .position = {0}, .rotation = geo_quat_ident);
}

static void app_window_fullscreen_toggle(GapWindowComp* win) {
  if (gap_window_mode(win) == GapWindowMode_Fullscreen) {
    // Enter windowed mode.
    gap_window_resize(
        win, gap_window_param(win, GapParam_WindowSizePreFullscreen), GapWindowMode_Windowed);
    // Release cursor confinement.
    gap_window_flags_unset(win, GapWindowFlags_CursorConfine);
  } else {
    // Enter fullscreen mode.
    gap_window_resize(win, gap_vector(0, 0), GapWindowMode_Fullscreen);
    // Confine the cursor to the window (for multi-monitor setups).
    gap_window_flags_set(win, GapWindowFlags_CursorConfine);
  }
}

ecs_view_define(AppUpdateGlobalView) { ecs_access_read(InputManagerComp); }

ecs_view_define(WindowView) {
  ecs_access_read(AppWindowComp);
  ecs_access_write(GapWindowComp);
}
ecs_view_define(DebugMenuView) { ecs_access_read(DebugMenuComp); }

ecs_system_define(AppUpdateSys) {
  EcsView*     globalView = ecs_world_view_t(world, AppUpdateGlobalView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (!globalItr) {
    return;
  }
  const InputManagerComp* input = ecs_view_read_t(globalItr, InputManagerComp);
  if (input_triggered_lit(input, "Reset")) {
    scene_level_load(world, string_lit("levels/default.lvl"));
  }
  if (input_triggered_lit(input, "WindowNew")) {
    app_window_create(world);
  }

  EcsView*     windowView      = ecs_world_view_t(world, WindowView);
  EcsIterator* activeWindowItr = ecs_view_maybe_at(windowView, input_active_window(input));
  if (activeWindowItr) {
    const AppWindowComp* appWindow = ecs_view_read_t(activeWindowItr, AppWindowComp);
    GapWindowComp*       win       = ecs_view_write_t(activeWindowItr, GapWindowComp);

    const EcsEntityId debugMenu = appWindow->debugMenu;
    DebugMenuEvents   dbgEvents = 0;
    if (debugMenu) {
      const DebugMenuComp* menu = ecs_utils_read_t(world, DebugMenuView, debugMenu, DebugMenuComp);
      dbgEvents                 = debug_menu_events(menu);
    }

    if (input_triggered_lit(input, "WindowClose") || dbgEvents & DebugMenuEvents_CloseWindow) {
      log_i("Close window");
      gap_window_close(win);
    }
    if (input_triggered_lit(input, "WindowFullscreen") || dbgEvents & DebugMenuEvents_Fullscreen) {
      log_i("Toggle fullscreen");
      app_window_fullscreen_toggle(win);
    }
  }
}

ecs_module_init(game_app_module) {
  ecs_register_comp(AppWindowComp);

  ecs_register_view(AppUpdateGlobalView);
  ecs_register_view(WindowView);
  ecs_register_view(DebugMenuView);

  ecs_register_system(
      AppUpdateSys,
      ecs_view_id(AppUpdateGlobalView),
      ecs_view_id(WindowView),
      ecs_view_id(DebugMenuView));
}

static CliId g_assetFlag, g_helpFlag;

void app_ecs_configure(CliApp* app) {
  cli_app_register_desc(app, string_lit("Volo RTS Demo"));

  g_assetFlag = cli_register_flag(app, 'a', string_lit("assets"), CliOptionFlags_Value);
  cli_register_desc(app, g_assetFlag, string_lit("Path to asset directory."));
  cli_register_validator(app, g_assetFlag, cli_validate_file_directory);

  g_helpFlag = cli_register_flag(app, 'h', string_lit("help"), CliOptionFlags_None);
  cli_register_desc(app, g_helpFlag, string_lit("Display this help page."));
  cli_register_exclusions(app, g_helpFlag, g_assetFlag);
}

bool app_ecs_validate(const CliApp* app, const CliInvocation* invoc) {
  if (cli_parse_provided(invoc, g_helpFlag)) {
    cli_help_write_file(app, g_file_stderr);
    return false;
  }
  return true;
}

void app_ecs_register(EcsDef* def, MAYBE_UNUSED const CliInvocation* invoc) {
  asset_register(def);
  debug_register(def);
  gap_register(def);
  input_register(def);
  rend_register(def);
  scene_register(def);
  ui_register(def);
  vfx_register(def);
  snd_register(def);

  ecs_register_module(def, game_app_module);
  ecs_register_module(def, game_cmd_module);
  ecs_register_module(def, game_input_module);
}

void app_ecs_init(EcsWorld* world, const CliInvocation* invoc) {
  const String assetPath = cli_read_string(invoc, g_assetFlag, string_lit("assets"));
  if (file_stat_path_sync(assetPath).type != FileType_Directory) {
    log_e("Asset directory not found", log_param("path", fmt_path(assetPath)));
    return;
  }
  AssetManagerComp* assets = asset_manager_create_fs(
      world, AssetManagerFlags_TrackChanges | AssetManagerFlags_DelayUnload, assetPath);

  app_ambiance_create(world, assets);

  scene_level_load(world, string_lit("levels/default.lvl"));
  input_resource_init(world, string_lit("global/game-input.imp"));
  scene_prefab_init(world, string_lit("global/game-prefabs.pfb"));
  scene_weapon_init(world, string_lit("global/game-weapons.wea"));
  scene_terrain_init(
      world,
      string_lit("graphics/scene/terrain.gra"),
      string_lit("external/terrain/terrain_3_height.r16"));

  app_window_create(world);
}

bool app_ecs_should_quit(EcsWorld* world) { return !ecs_utils_any(world, WindowView); }
