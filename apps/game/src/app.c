#include "app_ecs.h"
#include "asset.h"
#include "core_file.h"
#include "core_float.h"
#include "core_math.h"
#include "debug.h"
#include "ecs.h"
#include "gap.h"
#include "input.h"
#include "input_resource.h"
#include "log_logger.h"
#include "rend_register.h"
#include "rend_settings.h"
#include "scene_camera.h"
#include "scene_level.h"
#include "scene_prefab.h"
#include "scene_register.h"
#include "scene_sound.h"
#include "scene_terrain.h"
#include "scene_time.h"
#include "scene_transform.h"
#include "scene_weapon.h"
#include "snd_mixer.h"
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

typedef enum {
  AppQuality_UltraLow,
  AppQuality_Low,
  AppQuality_Medium,
  AppQuality_High,

  AppQuality_Count,
} AppQuality;

static const String g_qualityLabels[] = {
    string_static("UltraLow"),
    string_static("Low"),
    string_static("Medium"),
    string_static("High"),
};
ASSERT(array_elems(g_qualityLabels) == AppQuality_Count, "Incorrect number of quality labels");

ecs_comp_define(AppComp) {
  AppMode    mode : 8;
  bool       powerSaving;
  AppQuality quality;
};

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

static void app_quality_apply(
    AppComp* app, RendSettingsGlobalComp* rendSetGlobal, RendSettingsComp* rendSetWin) {
  if (app->powerSaving) {
    rendSetGlobal->limiterFreq = 30;
  } else {
    rendSetGlobal->limiterFreq = 0;
  }
  static const RendFlags g_rendOptionalFeatures = RendFlags_AmbientOcclusion | RendFlags_Bloom |
                                                  RendFlags_Distortion | RendFlags_ParticleShadows;
  switch (app->quality) {
  case AppQuality_UltraLow:
    rendSetGlobal->flags &= ~RendGlobalFlags_SunShadows;
    rendSetWin->flags &= ~g_rendOptionalFeatures;
    rendSetWin->resolutionScale = 0.75f;
    break;
  case AppQuality_Low:
    rendSetGlobal->flags |= RendGlobalFlags_SunShadows;
    rendSetWin->flags &= ~g_rendOptionalFeatures;
    rendSetWin->resolutionScale  = 0.75f;
    rendSetWin->shadowResolution = 1024;
    break;
  case AppQuality_Medium:
    rendSetGlobal->flags |= RendGlobalFlags_SunShadows;
    rendSetWin->flags |= g_rendOptionalFeatures;
    rendSetWin->resolutionScale           = 1.0f;
    rendSetWin->aoResolutionScale         = 0.75f;
    rendSetWin->shadowResolution          = 2048;
    rendSetWin->bloomSteps                = 5;
    rendSetWin->distortionResolutionScale = 0.25f;
    break;
  case AppQuality_High:
    rendSetGlobal->flags |= RendGlobalFlags_SunShadows;
    rendSetWin->flags |= g_rendOptionalFeatures;
    rendSetWin->resolutionScale           = 1.0f;
    rendSetWin->aoResolutionScale         = 1.0f;
    rendSetWin->shadowResolution          = 4096;
    rendSetWin->bloomSteps                = 6;
    rendSetWin->distortionResolutionScale = 1.0f;
    break;
  case AppQuality_Count:
    UNREACHABLE
  }
}

typedef struct {
  AppComp*                app;
  const InputManagerComp* input;
  SndMixerComp*           soundMixer;
  SceneTimeSettingsComp*  timeSet;
  CmdControllerComp*      cmd;
  GapWindowComp*          win;
  RendSettingsGlobalComp* rendSetGlobal;
  RendSettingsComp*       rendSetWin;
} AppActionContext;

static void app_action_debug_draw(UiCanvasComp* canvas, const AppActionContext* ctx) {
  const bool isInDebugMode = ctx->app->mode == AppMode_Debug;
  if (ui_button(
          canvas,
          .label      = ui_shape_scratch(UiShape_Bug),
          .fontSize   = 35,
          .tooltip    = string_lit("Enable / disable debug mode."),
          .frameColor = isInDebugMode ? ui_color(178, 0, 0, 192) : ui_color(32, 32, 32, 192)) ||
      input_triggered_lit(ctx->input, "AppDebug")) {

    log_i("Toggle debug-mode", log_param("debug", fmt_bool(!isInDebugMode)));
    ctx->app->mode ^= AppMode_Debug;
    cmd_push_deselect_all(ctx->cmd);
  }
}

static void app_action_pause_draw(UiCanvasComp* canvas, const AppActionContext* ctx) {
  const bool isPaused = (ctx->timeSet->flags & SceneTimeFlags_Paused) != 0;
  if (ui_button(
          canvas,
          .label      = ui_shape_scratch(UiShape_Pause),
          .fontSize   = 35,
          .tooltip    = string_lit("Pause / Resume."),
          .frameColor = isPaused ? ui_color(0, 178, 0, 192) : ui_color(32, 32, 32, 192))) {

    log_i("Toggle pause", log_param("paused", fmt_bool(!isPaused)));
    ctx->timeSet->flags ^= SceneTimeFlags_Paused;
  }
}

static void app_action_sound_draw(UiCanvasComp* canvas, const AppActionContext* ctx) {
  static const UiVector g_popupSize    = {.x = 35.0f, .y = 100.0f};
  static const f32      g_popupSpacing = 8.0f;
  static const UiVector g_popupInset   = {.x = -15.0f, .y = -15.0f};

  f32                     volume      = snd_mixer_gain_get(ctx->soundMixer) * 1e2f;
  const bool              muted       = volume <= f32_epsilon;
  const UiId              popupId     = ui_canvas_id_peek(canvas);
  const UiPersistentFlags popupFlags  = ui_canvas_persistent_flags(canvas, popupId);
  const bool              popupActive = (popupFlags & UiPersistentFlags_Open) != 0;

  ui_canvas_id_block_next(canvas);

  if (ui_button(
          canvas,
          .label      = ui_shape_scratch(muted ? UiShape_VolumeOff : UiShape_VolumeUp),
          .fontSize   = 35,
          .frameColor = popupActive ? ui_color(128, 128, 128, 192) : ui_color(32, 32, 32, 192),
          .tooltip    = string_lit("Open / Close the sound volume controls."))) {
    ui_canvas_persistent_flags_toggle(canvas, popupId, UiPersistentFlags_Open);
  }

  if (popupActive) {
    ui_layout_push(canvas);
    ui_layout_move(canvas, ui_vector(0.5f, 1.0f), UiBase_Current, Ui_XY);
    ui_layout_move_dir(canvas, Ui_Up, g_popupSpacing, UiBase_Absolute);
    ui_layout_resize(canvas, UiAlign_BottomCenter, g_popupSize, UiBase_Absolute, Ui_XY);

    // Popup background.
    ui_style_push(canvas);
    ui_style_outline(canvas, 2);
    ui_style_color(canvas, ui_color(128, 128, 128, 192));
    ui_canvas_draw_glyph(canvas, UiShape_Circle, 5, UiFlags_Interactable);
    ui_style_pop(canvas);

    // Volume slider.
    ui_layout_grow(canvas, UiAlign_MiddleCenter, g_popupInset, UiBase_Absolute, Ui_XY);
    if (ui_slider(
            canvas,
            &volume,
            .vertical = true,
            .max      = 1e2f,
            .step     = 1,
            .tooltip  = string_lit("Sound volume."))) {
      snd_mixer_gain_set(ctx->soundMixer, volume * 1e-2f);
    }
    ui_layout_pop(canvas);

    // Close when pressing outside.
    if (ui_canvas_input_any(canvas) && ui_canvas_group_block_status(canvas) == UiStatus_Idle) {
      ui_canvas_persistent_flags_unset(canvas, popupId, UiPersistentFlags_Open);
    }
  }

  ui_canvas_id_block_next(canvas); // End on an consistent id.
}

static void app_action_quality_draw(UiCanvasComp* canvas, const AppActionContext* ctx) {
  static const UiVector g_popupSize    = {.x = 250.0f, .y = 70.0f};
  static const f32      g_popupSpacing = 8.0f;

  const UiId              popupId     = ui_canvas_id_peek(canvas);
  const UiPersistentFlags popupFlags  = ui_canvas_persistent_flags(canvas, popupId);
  const bool              popupActive = (popupFlags & UiPersistentFlags_Open) != 0;

  ui_canvas_id_block_next(canvas);

  if (ui_button(
          canvas,
          .label      = ui_shape_scratch(UiShape_Image),
          .fontSize   = 35,
          .frameColor = popupActive ? ui_color(128, 128, 128, 192) : ui_color(32, 32, 32, 192),
          .tooltip    = string_lit("Open / Close the quality controls."))) {
    ui_canvas_persistent_flags_toggle(canvas, popupId, UiPersistentFlags_Open);
  }

  if (popupActive && ctx->rendSetGlobal && ctx->rendSetWin) {
    ui_layout_push(canvas);
    ui_layout_move(canvas, ui_vector(0.5f, 1.0f), UiBase_Current, Ui_XY);
    ui_layout_move_dir(canvas, Ui_Up, g_popupSpacing, UiBase_Absolute);
    ui_layout_resize(canvas, UiAlign_BottomCenter, g_popupSize, UiBase_Absolute, Ui_XY);

    // Popup background.
    ui_style_push(canvas);
    ui_style_outline(canvas, 2);
    ui_style_color(canvas, ui_color(128, 128, 128, 192));
    ui_canvas_draw_glyph(canvas, UiShape_Circle, 5, UiFlags_Interactable);
    ui_style_pop(canvas);

    // Settings.
    ui_layout_container_push(canvas, UiClip_None);

    UiTable table = ui_table();
    ui_table_add_column(&table, UiTableColumn_Fixed, 125);
    ui_table_add_column(&table, UiTableColumn_Fixed, 110);

    ui_table_next_row(canvas, &table);
    ui_label(canvas, string_lit("PowerSaving"));
    ui_table_next_column(canvas, &table);
    if (ui_toggle(canvas, &ctx->app->powerSaving)) {
      app_quality_apply(ctx->app, ctx->rendSetGlobal, ctx->rendSetWin);
    }

    ui_table_next_row(canvas, &table);
    ui_label(canvas, string_lit("Quality"));
    ui_table_next_column(canvas, &table);
    const u32 qualityCount = AppQuality_Count;
    if (ui_select(canvas, (i32*)&ctx->app->quality, g_qualityLabels, qualityCount, .dir = Ui_Up)) {
      app_quality_apply(ctx->app, ctx->rendSetGlobal, ctx->rendSetWin);
    }

    ui_layout_container_pop(canvas);
    ui_layout_pop(canvas);

    // Close when pressing outside.
    if (ui_canvas_input_any(canvas) && ui_canvas_group_block_status(canvas) == UiStatus_Idle) {
      ui_canvas_persistent_flags_unset(canvas, popupId, UiPersistentFlags_Open);
    }
  }

  ui_canvas_id_block_next(canvas); // End on an consistent id.
}

static void app_action_fullscreen_draw(UiCanvasComp* canvas, const AppActionContext* ctx) {
  if (ui_button(
          canvas,
          .label    = ui_shape_scratch(UiShape_Fullscreen),
          .fontSize = 35,
          .tooltip  = string_lit("Enter / exit fullscreen.")) ||
      input_triggered_lit(ctx->input, "AppWindowFullscreen")) {

    log_i("Toggle fullscreen");
    app_window_fullscreen_toggle(ctx->win);
  }
}

static void app_action_exit_draw(UiCanvasComp* canvas, const AppActionContext* ctx) {
  if (ui_button(
          canvas,
          .label    = ui_shape_scratch(UiShape_Logout),
          .fontSize = 35,
          .tooltip  = string_lit("Close the window.")) ||
      input_triggered_lit(ctx->input, "AppWindowClose")) {

    log_i("Close window");
    gap_window_close(ctx->win);
  }
}

static void app_action_bar_draw(UiCanvasComp* canvas, const AppActionContext* ctx) {
  static void (*const g_actions[])(UiCanvasComp*, const AppActionContext*) = {
      app_action_debug_draw,
      app_action_pause_draw,
      app_action_sound_draw,
      app_action_quality_draw,
      app_action_fullscreen_draw,
      app_action_exit_draw,
  };
  static const u32      g_actionCount = array_elems(g_actions);
  static const UiVector g_buttonSize  = {.x = 50.0f, .y = 50.0f};
  static const f32      g_spacing     = 8.0f;

  const f32 xCenterOffset = (g_actionCount - 1) * (g_buttonSize.x + g_spacing) * -0.5f;
  ui_layout_inner(canvas, UiBase_Canvas, UiAlign_BottomCenter, g_buttonSize, UiBase_Absolute);
  ui_layout_move(canvas, ui_vector(xCenterOffset, g_spacing), UiBase_Absolute, Ui_XY);

  for (u32 i = 0; i != g_actionCount; ++i) {
    g_actions[i](canvas, ctx);
    ui_layout_next(canvas, Ui_Right, g_spacing);
  }
}

ecs_view_define(AppUpdateGlobalView) {
  ecs_access_write(AppComp);
  ecs_access_write(CmdControllerComp);
  ecs_access_write(InputManagerComp);
  ecs_access_write(RendSettingsGlobalComp);
  ecs_access_write(SceneTimeSettingsComp);
  ecs_access_write(SndMixerComp);
}

ecs_view_define(WindowView) {
  ecs_access_maybe_write(DebugStatsComp);
  ecs_access_maybe_write(RendSettingsComp);
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
  AppComp*                app           = ecs_view_write_t(globalItr, AppComp);
  CmdControllerComp*      cmd           = ecs_view_write_t(globalItr, CmdControllerComp);
  RendSettingsGlobalComp* rendSetGlobal = ecs_view_write_t(globalItr, RendSettingsGlobalComp);
  SndMixerComp*           soundMixer    = ecs_view_write_t(globalItr, SndMixerComp);
  SceneTimeSettingsComp*  timeSet       = ecs_view_write_t(globalItr, SceneTimeSettingsComp);

  InputManagerComp* input = ecs_view_write_t(globalItr, InputManagerComp);
  if (input_triggered_lit(input, "AppReset")) {
    scene_level_load(world, string_lit("levels/default.lvl"));
  }

  EcsIterator* canvasItr = ecs_view_itr(ecs_world_view_t(world, UiCanvasView));

  EcsView*     windowView      = ecs_world_view_t(world, WindowView);
  EcsIterator* activeWindowItr = ecs_view_maybe_at(windowView, input_active_window(input));
  if (activeWindowItr) {
    const EcsEntityId windowEntity    = ecs_view_entity(activeWindowItr);
    AppWindowComp*    appWindow       = ecs_view_write_t(activeWindowItr, AppWindowComp);
    GapWindowComp*    win             = ecs_view_write_t(activeWindowItr, GapWindowComp);
    DebugStatsComp*   stats           = ecs_view_write_t(activeWindowItr, DebugStatsComp);
    RendSettingsComp* rendSettingsWin = ecs_view_write_t(activeWindowItr, RendSettingsComp);

    if (ecs_view_maybe_jump(canvasItr, appWindow->uiCanvas)) {
      UiCanvasComp* canvas = ecs_view_write_t(canvasItr, UiCanvasComp);
      ui_canvas_reset(canvas);
      app_action_bar_draw(
          canvas,
          &(AppActionContext){
              .app           = app,
              .input         = input,
              .soundMixer    = soundMixer,
              .timeSet       = timeSet,
              .cmd           = cmd,
              .win           = win,
              .rendSetGlobal = rendSetGlobal,
              .rendSetWin    = rendSettingsWin,
          });
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

  ecs_world_add_t(world, ecs_world_global(world), AppComp, .quality = AppQuality_Medium);
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
