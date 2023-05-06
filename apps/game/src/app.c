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
#include "ui.h"
#include "ui_register.h"
#include "vfx_register.h"

#include "cmd_internal.h"

static const GapVector g_appWindowSize = {1920, 1080};

typedef enum {
  AppMode_Normal,
  AppMode_Debug,
} AppMode;

ecs_comp_define(AppComp) { AppMode mode; };

ecs_comp_define(AppWindowComp) {
  EcsEntityId uiCanvas;
  EcsEntityId debugMenu;
  EcsEntityId debugLogViewer;
};

static void app_ambiance_create(EcsWorld* world, AssetManagerComp* assets) {
  ecs_world_add_t(
      world,
      ecs_world_entity_create(world),
      SceneSoundComp,
      .asset   = asset_lookup(world, assets, string_lit("external/sound/ambiance-01.wav")),
      .pitch   = 1.0f,
      .gain    = 0.4f,
      .looping = true);
}

static void app_window_create(EcsWorld* world) {
  const EcsEntityId window   = gap_window_create(world, GapWindowFlags_Default, g_appWindowSize);
  const EcsEntityId uiCanvas = ui_canvas_create(world, window, UiCanvasCreateFlags_ToFront);

  ecs_world_add_t(world, window, AppWindowComp, .uiCanvas = uiCanvas);

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

static void app_action_bar_draw(
    UiCanvasComp*           canvas,
    AppComp*                app,
    const InputManagerComp* input,
    CmdControllerComp*      cmd,
    GapWindowComp*          win) {
  static const u32      g_buttonCount = 3;
  static const UiVector g_buttonSize  = {.x = 50.0f, .y = 50.0f};
  static const f32      g_spacing     = 8.0f;
  static const u16      g_iconSize    = 35;

  const f32 xCenterOffset = (g_buttonCount - 1) * (g_buttonSize.x + g_spacing) * -0.5f;
  ui_layout_inner(canvas, UiBase_Canvas, UiAlign_BottomCenter, g_buttonSize, UiBase_Absolute);
  ui_layout_move(canvas, ui_vector(xCenterOffset, g_spacing), UiBase_Absolute, Ui_XY);

  if (ui_button(
          canvas,
          .label      = ui_shape_scratch(UiShape_Bug),
          .fontSize   = g_iconSize,
          .tooltip    = string_lit("Enable / disable debug mode."),
          .frameColor = app->mode ? ui_color(178, 0, 0, 192) : ui_color(32, 32, 32, 192)) ||
      input_triggered_lit(input, "AppDebug")) {

    log_i("Toggle debug-mode");
    app->mode ^= AppMode_Debug;
    cmd_push_deselect_all(cmd);
  }

  ui_layout_next(canvas, Ui_Right, g_spacing);

  if (ui_button(
          canvas,
          .label    = ui_shape_scratch(UiShape_Fullscreen),
          .fontSize = g_iconSize,
          .tooltip  = string_lit("Enter / exit fullscreen.")) ||
      input_triggered_lit(input, "AppWindowFullscreen")) {

    log_i("Toggle fullscreen");
    app_window_fullscreen_toggle(win);
  }

  ui_layout_next(canvas, Ui_Right, g_spacing);

  if (ui_button(
          canvas,
          .label    = ui_shape_scratch(UiShape_Logout),
          .fontSize = g_iconSize,
          .tooltip  = string_lit("Close the window.")) ||
      input_triggered_lit(input, "AppWindowClose")) {

    log_i("Close window");
    gap_window_close(win);
  }
}

ecs_view_define(AppUpdateGlobalView) {
  ecs_access_write(AppComp);
  ecs_access_write(CmdControllerComp);
  ecs_access_write(InputManagerComp);
}

ecs_view_define(WindowView) {
  ecs_access_maybe_write(DebugStatsComp);
  ecs_access_write(AppWindowComp);
  ecs_access_write(GapWindowComp);
}

ecs_view_define(UiCanvasView) { ecs_access_write(UiCanvasComp); }

ecs_system_define(AppUpdateSys) {
  EcsView*     globalView = ecs_world_view_t(world, AppUpdateGlobalView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (!globalItr) {
    return;
  }
  AppComp*           app = ecs_view_write_t(globalItr, AppComp);
  CmdControllerComp* cmd = ecs_view_write_t(globalItr, CmdControllerComp);

  InputManagerComp* input = ecs_view_write_t(globalItr, InputManagerComp);
  if (input_triggered_lit(input, "AppReset")) {
    scene_level_load(world, string_lit("levels/default.lvl"));
  }

  EcsIterator* canvasItr = ecs_view_itr(ecs_world_view_t(world, UiCanvasView));

  EcsView*     windowView      = ecs_world_view_t(world, WindowView);
  EcsIterator* activeWindowItr = ecs_view_maybe_at(windowView, input_active_window(input));
  if (activeWindowItr) {
    const EcsEntityId windowEntity = ecs_view_entity(activeWindowItr);
    AppWindowComp*    appWindow    = ecs_view_write_t(activeWindowItr, AppWindowComp);
    GapWindowComp*    win          = ecs_view_write_t(activeWindowItr, GapWindowComp);
    DebugStatsComp*   stats        = ecs_view_write_t(activeWindowItr, DebugStatsComp);

    if (ecs_view_maybe_jump(canvasItr, appWindow->uiCanvas)) {
      UiCanvasComp* canvas = ecs_view_write_t(canvasItr, UiCanvasComp);
      ui_canvas_reset(canvas);
      app_action_bar_draw(canvas, app, input, cmd, win);
    }

    // clang-format off
    switch (app->mode) {
    case AppMode_Normal:
      if (appWindow->debugMenu)       { ecs_world_entity_destroy(world, appWindow->debugMenu); appWindow->debugMenu = 0; }
      if (appWindow->debugLogViewer)  { ecs_world_entity_destroy(world, appWindow->debugLogViewer); appWindow->debugLogViewer = 0; }
      if (stats)                      { debug_stats_show_set(stats, DebugStatShow_Minimal); }
      input_layer_disable(input, string_hash_lit("Debug"));
      input_layer_enable(input, string_hash_lit("Game"));
      break;
    case AppMode_Debug:
      if (!appWindow->debugMenu)      { appWindow->debugMenu = debug_menu_create(world, windowEntity); }
      if (!appWindow->debugLogViewer) { appWindow->debugLogViewer = debug_log_viewer_create(world, windowEntity); }
      if (stats)                      { debug_stats_show_set(stats, DebugStatShow_Full); }
      input_layer_enable(input, string_hash_lit("Debug"));
      input_layer_disable(input, string_hash_lit("Game"));
      break;
    }
    // clang-format on
  }
}

ecs_module_init(game_app_module) {
  ecs_register_comp(AppComp);
  ecs_register_comp(AppWindowComp);

  ecs_register_view(AppUpdateGlobalView);
  ecs_register_view(WindowView);
  ecs_register_view(UiCanvasView);

  ecs_register_system(
      AppUpdateSys,
      ecs_view_id(AppUpdateGlobalView),
      ecs_view_id(WindowView),
      ecs_view_id(UiCanvasView));
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

  ecs_world_add_t(world, ecs_world_global(world), AppComp);
  app_window_create(world);

  AssetManagerComp* assets = asset_manager_create_fs(
      world, AssetManagerFlags_TrackChanges | AssetManagerFlags_DelayUnload, assetPath);

  app_ambiance_create(world, assets);

  InputResourceComp* inputResource = input_resource_init(world);
  input_resource_load_map(inputResource, string_lit("global/app-input.imp"));
  input_resource_load_map(inputResource, string_lit("global/game-input.imp"));
  input_resource_load_map(inputResource, string_lit("global/debug-input.imp"));

  scene_level_load(world, string_lit("levels/default.lvl"));
  scene_prefab_init(world, string_lit("global/game-prefabs.pfb"));
  scene_weapon_init(world, string_lit("global/game-weapons.wea"));
  scene_terrain_init(
      world,
      string_lit("graphics/scene/terrain.gra"),
      string_lit("external/terrain/terrain_3_height.r16"));
}

bool app_ecs_should_quit(EcsWorld* world) { return !ecs_utils_any(world, WindowView); }
