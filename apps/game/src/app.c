#include "app_ecs.h"
#include "asset_manager.h"
#include "asset_register.h"
#include "cli_app.h"
#include "cli_help.h"
#include "cli_parse.h"
#include "cli_read.h"
#include "cli_validate.h"
#include "core_file.h"
#include "core_float.h"
#include "core_math.h"
#include "debug_log_viewer.h"
#include "debug_menu.h"
#include "debug_panel.h"
#include "debug_register.h"
#include "debug_stats.h"
#include "ecs_utils.h"
#include "ecs_view.h"
#include "ecs_world.h"
#include "gap_register.h"
#include "gap_vector.h"
#include "gap_window.h"
#include "input_manager.h"
#include "input_register.h"
#include "input_resource.h"
#include "log_logger.h"
#include "rend_register.h"
#include "rend_settings.h"
#include "scene_camera.h"
#include "scene_level.h"
#include "scene_prefab.h"
#include "scene_product.h"
#include "scene_register.h"
#include "scene_time.h"
#include "scene_transform.h"
#include "scene_visibility.h"
#include "scene_weapon.h"
#include "snd_mixer.h"
#include "snd_register.h"
#include "ui_canvas.h"
#include "ui_layout.h"
#include "ui_register.h"
#include "ui_shape.h"
#include "ui_style.h"
#include "ui_table.h"
#include "ui_widget.h"
#include "vfx_register.h"

#include "cmd_internal.h"
#include "hud_internal.h"
#include "prefs_internal.h"

static const String g_appLevel = string_static("levels/default.level");

typedef enum {
  AppMode_Normal,
  AppMode_Debug,
} AppMode;

ecs_comp_define(AppComp) {
  AppMode     mode : 8;
  EcsEntityId mainWindow;
};

ecs_comp_define(AppMainWindowComp) {
  EcsEntityId uiCanvas;
  EcsEntityId debugMenu;
  EcsEntityId debugLogViewer;
};

static EcsEntityId app_main_window_create(
    EcsWorld*         world,
    AssetManagerComp* assets,
    const bool        fullscreen,
    const u16         width,
    const u16         height) {
  GapWindowFlags flags = GapWindowFlags_Default;
  flags |= fullscreen ? GapWindowFlags_CursorConfine : 0;

  const GapVector     size   = {.width = (i32)width, .height = (i32)height};
  const GapWindowMode mode   = fullscreen ? GapWindowMode_Fullscreen : GapWindowMode_Windowed;
  const GapIcon       icon   = GapIcon_Main;
  const String        title  = string_empty; // Use default title.
  const EcsEntityId   window = gap_window_create(world, mode, flags, size, icon, title);

  const EcsEntityId uiCanvas       = ui_canvas_create(world, window, UiCanvasCreateFlags_ToFront);
  const EcsEntityId debugLogViewer = debug_log_viewer_create(world, window, LogMask_None);

  ecs_world_add_t(
      world, window, AppMainWindowComp, .uiCanvas = uiCanvas, .debugLogViewer = debugLogViewer);

  ecs_world_add_t(
      world,
      window,
      SceneCameraComp,
      .persFov   = 50 * math_deg_to_rad,
      .persNear  = 0.75f,
      .orthoSize = 5);

  ecs_world_add_empty_t(world, window, SceneSoundListenerComp);
  ecs_world_add_t(world, window, SceneTransformComp, .position = {0}, .rotation = geo_quat_ident);
  hud_init(world, assets, window);

  return window;
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
    const GamePrefsComp*    prefs,
    RendSettingsGlobalComp* rendSetGlobal,
    RendSettingsComp*       rendSetWin) {
  if (prefs->powerSaving) {
    rendSetGlobal->limiterFreq = 30;
  } else {
    rendSetGlobal->limiterFreq = 0;
  }
  // clang-format off
  static const RendFlags g_rendLowFeatures    = RendFlags_Shadows;
  static const RendFlags g_rendMediumFeatures = RendFlags_AmbientOcclusion |
                                                RendFlags_Bloom            |
                                                RendFlags_Distortion       |
                                                RendFlags_VfxShadows;
  // clang-format on
  switch (prefs->quality) {
  case GameQuality_VeryLow:
    rendSetWin->flags &= ~(g_rendLowFeatures | g_rendMediumFeatures);
    rendSetWin->resolutionScale = 0.75f;
    break;
  case GameQuality_Low:
    rendSetWin->flags |= g_rendLowFeatures;
    rendSetWin->flags &= ~g_rendMediumFeatures;
    rendSetWin->resolutionScale  = 0.75f;
    rendSetWin->shadowResolution = 1024;
    break;
  case GameQuality_Medium:
    rendSetWin->flags |= g_rendLowFeatures | g_rendMediumFeatures;
    rendSetWin->resolutionScale           = 1.0f;
    rendSetWin->aoResolutionScale         = 0.75f;
    rendSetWin->shadowResolution          = 2048;
    rendSetWin->bloomSteps                = 5;
    rendSetWin->distortionResolutionScale = 0.25f;
    break;
  case GameQuality_High:
    rendSetWin->flags |= g_rendLowFeatures | g_rendMediumFeatures;
    rendSetWin->resolutionScale           = 1.0f;
    rendSetWin->aoResolutionScale         = 1.0f;
    rendSetWin->shadowResolution          = 4096;
    rendSetWin->bloomSteps                = 6;
    rendSetWin->distortionResolutionScale = 1.0f;
    break;
  case GameQuality_Count:
    UNREACHABLE
  }
}

typedef struct {
  EcsWorld*               world;
  AppComp*                app;
  GamePrefsComp*          prefs;
  const InputManagerComp* input;
  SndMixerComp*           soundMixer;
  SceneTimeSettingsComp*  timeSet;
  CmdControllerComp*      cmd;
  GapWindowComp*          win;
  RendSettingsGlobalComp* rendSetGlobal;
  RendSettingsComp*       rendSetWin;
  DebugStatsGlobalComp*   debugStats;
} AppActionContext;

static void app_action_notify(const AppActionContext* ctx, const String action) {
  if (ctx->debugStats) {
    debug_stats_notify(ctx->debugStats, string_lit("Action"), action);
  }
}

static void app_action_debug_draw(UiCanvasComp* canvas, const AppActionContext* ctx) {
  const bool isInDebugMode = ctx->app->mode == AppMode_Debug;
  if (ui_button(
          canvas,
          .label      = ui_shape_scratch(UiShape_Bug),
          .fontSize   = 35,
          .tooltip    = string_lit("Enable / disable debug mode."),
          .frameColor = isInDebugMode ? ui_color(178, 0, 0, 192) : ui_color(32, 32, 32, 192),
          .activate   = input_triggered_lit(ctx->input, "AppDebug"))) {

    app_action_notify(ctx, isInDebugMode ? string_lit("Game mode") : string_lit("Debug mode"));
    log_i("Toggle debug-mode", log_param("debug", fmt_bool(!isInDebugMode)));

    ctx->app->mode ^= AppMode_Debug;
    cmd_push_deselect_all(ctx->cmd);

    if (ctx->app->mode == AppMode_Debug) {
      ctx->timeSet->flags |= SceneTimeFlags_Paused;
      ctx->rendSetWin->skyMode = RendSkyMode_Gradient;
    } else {
      ctx->timeSet->flags &= ~SceneTimeFlags_Paused;
      ctx->rendSetWin->skyMode = RendSkyMode_None;
    }
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

    app_action_notify(ctx, isPaused ? string_lit("Resume") : string_lit("Pause"));
    log_i("Toggle pause", log_param("paused", fmt_bool(!isPaused)));

    ctx->timeSet->flags ^= SceneTimeFlags_Paused;
  }
}

static void app_action_restart_draw(UiCanvasComp* canvas, const AppActionContext* ctx) {
  if (ui_button(
          canvas,
          .label    = ui_shape_scratch(UiShape_Restart),
          .fontSize = 35,
          .tooltip  = string_lit("Restart the level."),
          .activate = input_triggered_lit(ctx->input, "AppReset"))) {

    app_action_notify(ctx, string_lit("Restart"));
    log_i("Restart");

    scene_level_reload(ctx->world);
  }
}

static void app_action_sound_draw(UiCanvasComp* canvas, const AppActionContext* ctx) {
  static const UiVector g_popupSize    = {.x = 35.0f, .y = 100.0f};
  static const f32      g_popupSpacing = 8.0f;
  static const UiVector g_popupInset   = {.x = -15.0f, .y = -15.0f};

  const bool              muted       = ctx->prefs->volume <= f32_epsilon;
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
            &ctx->prefs->volume,
            .vertical = true,
            .max      = 1e2f,
            .step     = 1,
            .tooltip  = string_lit("Sound volume."))) {
      app_action_notify(
          ctx, fmt_write_scratch("Volume: {}", fmt_float(ctx->prefs->volume, .maxDecDigits = 0)));

      ctx->prefs->dirty = true;
      snd_mixer_gain_set(ctx->soundMixer, ctx->prefs->volume * 1e-2f);
    }
    ui_layout_pop(canvas);

    // Close when pressing outside.
    if (ui_canvas_input_any(canvas) && ui_canvas_group_block_inactive(canvas)) {
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
    if (ui_toggle(canvas, &ctx->prefs->powerSaving)) {
      app_action_notify(
          ctx, ctx->prefs->powerSaving ? string_lit("Power saving") : string_lit("Power normal"));

      ctx->prefs->dirty = true;
      app_quality_apply(ctx->prefs, ctx->rendSetGlobal, ctx->rendSetWin);
    }

    ui_table_next_row(canvas, &table);
    ui_label(canvas, string_lit("Quality"));
    ui_table_next_column(canvas, &table);
    i32* quality = (i32*)&ctx->prefs->quality;
    if (ui_select(canvas, quality, g_gameQualityLabels, GameQuality_Count, .dir = Ui_Up)) {
      app_action_notify(
          ctx, fmt_write_scratch("Quality {}", fmt_text(g_gameQualityLabels[*quality])));

      ctx->prefs->dirty = true;
      app_quality_apply(ctx->prefs, ctx->rendSetGlobal, ctx->rendSetWin);
    }

    ui_layout_container_pop(canvas);
    ui_layout_pop(canvas);

    // Close when pressing outside.
    if (ui_canvas_input_any(canvas) && ui_canvas_group_block_inactive(canvas)) {
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
          .tooltip  = string_lit("Enter / exit fullscreen."),
          .activate = input_triggered_lit(ctx->input, "AppWindowFullscreen"))) {

    if (gap_window_mode(ctx->win) == GapWindowMode_Fullscreen) {
      app_action_notify(ctx, string_lit("Windowed"));
    } else {
      app_action_notify(ctx, string_lit("Fullscreen"));
    }
    log_i("Toggle fullscreen");

    app_window_fullscreen_toggle(ctx->win);
  }
}

static void app_action_exit_draw(UiCanvasComp* canvas, const AppActionContext* ctx) {
  if (ui_button(
          canvas,
          .label    = ui_shape_scratch(UiShape_Logout),
          .fontSize = 35,
          .tooltip  = string_lit("Close the window."),
          .activate = input_triggered_lit(ctx->input, "AppWindowClose"))) {
    log_i("Close window");
    gap_window_close(ctx->win);
  }
}

static void app_action_bar_draw(UiCanvasComp* canvas, const AppActionContext* ctx) {
  static void (*const g_actions[])(UiCanvasComp*, const AppActionContext*) = {
      app_action_debug_draw,
      app_action_pause_draw,
      app_action_restart_draw,
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
  ecs_access_write(GamePrefsComp);
  ecs_access_write(InputManagerComp);
  ecs_access_write(RendSettingsGlobalComp);
  ecs_access_write(SceneTimeSettingsComp);
  ecs_access_write(SndMixerComp);
  ecs_access_write(SceneVisibilityEnvComp);
  ecs_access_maybe_write(DebugStatsGlobalComp);
}

ecs_view_define(MainWindowView) {
  ecs_access_maybe_write(DebugStatsComp);
  ecs_access_maybe_write(RendSettingsComp);
  ecs_access_write(AppMainWindowComp);
  ecs_access_write(GapWindowComp);
}

ecs_view_define(UiCanvasView) {
  ecs_view_flags(EcsViewFlags_Exclusive); // Only access the canvas's we create.
  ecs_access_write(UiCanvasComp);
}

ecs_view_define(DebugPanelView) { ecs_access_write(DebugPanelComp); }
ecs_view_define(DebugLogViewerView) { ecs_access_write(DebugLogViewerComp); }

static void app_debug_hide(EcsWorld* world, const bool hidden) {
  EcsView* debugPanelView = ecs_world_view_t(world, DebugPanelView);
  for (EcsIterator* itr = ecs_view_itr(debugPanelView); ecs_view_walk(itr);) {
    DebugPanelComp* panel = ecs_view_write_t(itr, DebugPanelComp);
    if (debug_panel_type(panel) != DebugPanelType_Detached) {
      debug_panel_hide(panel, hidden);
    }
  }
}

ecs_system_define(AppUpdateSys) {
  EcsView*     globalView = ecs_world_view_t(world, AppUpdateGlobalView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (!globalItr) {
    return;
  }
  AppComp*                app           = ecs_view_write_t(globalItr, AppComp);
  GamePrefsComp*          prefs         = ecs_view_write_t(globalItr, GamePrefsComp);
  CmdControllerComp*      cmd           = ecs_view_write_t(globalItr, CmdControllerComp);
  RendSettingsGlobalComp* rendSetGlobal = ecs_view_write_t(globalItr, RendSettingsGlobalComp);
  SndMixerComp*           soundMixer    = ecs_view_write_t(globalItr, SndMixerComp);
  SceneTimeSettingsComp*  timeSet       = ecs_view_write_t(globalItr, SceneTimeSettingsComp);
  InputManagerComp*       input         = ecs_view_write_t(globalItr, InputManagerComp);
  SceneVisibilityEnvComp* visibilityEnv = ecs_view_write_t(globalItr, SceneVisibilityEnvComp);
  DebugStatsGlobalComp*   debugStats    = ecs_view_write_t(globalItr, DebugStatsGlobalComp);

  EcsIterator* canvasItr         = ecs_view_itr(ecs_world_view_t(world, UiCanvasView));
  EcsIterator* debugLogViewerItr = ecs_view_itr(ecs_world_view_t(world, DebugLogViewerView));

  EcsView*     mainWinView = ecs_world_view_t(world, MainWindowView);
  EcsIterator* mainWinItr  = ecs_view_maybe_at(mainWinView, app->mainWindow);
  if (mainWinItr) {
    const EcsEntityId  windowEntity = ecs_view_entity(mainWinItr);
    AppMainWindowComp* appWindow    = ecs_view_write_t(mainWinItr, AppMainWindowComp);
    GapWindowComp*     win          = ecs_view_write_t(mainWinItr, GapWindowComp);
    DebugStatsComp*    stats        = ecs_view_write_t(mainWinItr, DebugStatsComp);
    RendSettingsComp*  rendSetWin   = ecs_view_write_t(mainWinItr, RendSettingsComp);

    // Save last window size.
    if (gap_window_events(win) & GapWindowEvents_Resized) {
      prefs->fullscreen = gap_window_mode(win) == GapWindowMode_Fullscreen;
      if (!prefs->fullscreen) {
        prefs->windowWidth  = gap_window_param(win, GapParam_WindowSize).width;
        prefs->windowHeight = gap_window_param(win, GapParam_WindowSize).height;
      }
      prefs->dirty = true;
    }

    if (ecs_view_maybe_jump(canvasItr, appWindow->uiCanvas)) {
      UiCanvasComp* canvas = ecs_view_write_t(canvasItr, UiCanvasComp);
      ui_canvas_reset(canvas);
      app_action_bar_draw(
          canvas,
          &(AppActionContext){
              .world         = world,
              .app           = app,
              .prefs         = prefs,
              .input         = input,
              .soundMixer    = soundMixer,
              .timeSet       = timeSet,
              .cmd           = cmd,
              .win           = win,
              .rendSetGlobal = rendSetGlobal,
              .rendSetWin    = rendSetWin,
              .debugStats    = debugStats,
          });
    }

    DebugLogViewerComp* debugLogViewer = null;
    if (ecs_view_maybe_jump(debugLogViewerItr, appWindow->debugLogViewer)) {
      debugLogViewer = ecs_view_write_t(debugLogViewerItr, DebugLogViewerComp);
    }

    // clang-format off
    switch (app->mode) {
    case AppMode_Normal:
      if (debugLogViewer)         { debug_log_viewer_set_mask(debugLogViewer, LogMask_Warn | LogMask_Error); }
      if (stats)                  { debug_stats_show_set(stats, DebugStatShow_Minimal); }
      app_debug_hide(world, true);
      input_layer_disable(input, string_hash_lit("Debug"));
      input_layer_enable(input, string_hash_lit("Game"));
      scene_visibility_flags_clear(visibilityEnv, SceneVisibilityFlags_ForceRender);
      break;
    case AppMode_Debug:
      if (!appWindow->debugMenu)  { appWindow->debugMenu = debug_menu_create(world, windowEntity); }
      if (debugLogViewer)         { debug_log_viewer_set_mask(debugLogViewer, LogMask_All); }
      if (stats)                  { debug_stats_show_set(stats, DebugStatShow_Full); }
      app_debug_hide(world, false);
      input_layer_enable(input, string_hash_lit("Debug"));
      input_layer_disable(input, string_hash_lit("Game"));
      scene_visibility_flags_set(visibilityEnv, SceneVisibilityFlags_ForceRender);
      break;
    }
    // clang-format on
  }
}

ecs_module_init(game_app_module) {
  ecs_register_comp(AppComp);
  ecs_register_comp(AppMainWindowComp);

  ecs_register_view(AppUpdateGlobalView);
  ecs_register_view(MainWindowView);
  ecs_register_view(UiCanvasView);
  ecs_register_view(DebugPanelView);
  ecs_register_view(DebugLogViewerView);

  ecs_register_system(
      AppUpdateSys,
      ecs_view_id(AppUpdateGlobalView),
      ecs_view_id(MainWindowView),
      ecs_view_id(UiCanvasView),
      ecs_view_id(DebugPanelView),
      ecs_view_id(DebugLogViewerView));
}

static CliId g_optAssets, g_optWindow, g_optWidth, g_optHeight, g_optHelp;

void app_ecs_configure(CliApp* app) {
  cli_app_register_desc(app, string_lit("Volo RTS Demo"));

  g_optAssets = cli_register_flag(app, 'a', string_lit("assets"), CliOptionFlags_Value);
  cli_register_desc(app, g_optAssets, string_lit("Path to asset directory."));
  cli_register_validator(app, g_optAssets, cli_validate_file_directory);

  g_optWindow = cli_register_flag(app, 'w', string_lit("window"), CliOptionFlags_None);
  cli_register_desc(app, g_optWindow, string_lit("Start the game in windowed mode."));

  g_optWidth = cli_register_flag(app, '\0', string_lit("width"), CliOptionFlags_Value);
  cli_register_desc(app, g_optWidth, string_lit("Game window width in pixels."));
  cli_register_validator(app, g_optWidth, cli_validate_u16);

  g_optHeight = cli_register_flag(app, '\0', string_lit("height"), CliOptionFlags_Value);
  cli_register_desc(app, g_optHeight, string_lit("Game window height in pixels."));
  cli_register_validator(app, g_optHeight, cli_validate_u16);

  g_optHelp = cli_register_flag(app, 'h', string_lit("help"), CliOptionFlags_None);
  cli_register_desc(app, g_optHelp, string_lit("Display this help page."));
  cli_register_exclusions(app, g_optHelp, g_optAssets);
  cli_register_exclusions(app, g_optHelp, g_optWindow);
  cli_register_exclusions(app, g_optHelp, g_optWidth);
  cli_register_exclusions(app, g_optHelp, g_optHeight);
}

bool app_ecs_validate(const CliApp* app, const CliInvocation* invoc) {
  if (cli_parse_provided(invoc, g_optHelp)) {
    cli_help_write_file(app, g_fileStdErr);
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
  snd_register(def);
  ui_register(def);
  vfx_register(def);

  ecs_register_module(def, game_app_module);
  ecs_register_module(def, game_cmd_module);
  ecs_register_module(def, game_hud_module);
  ecs_register_module(def, game_input_module);
  ecs_register_module(def, game_prefs_module);
}

void app_ecs_init(EcsWorld* world, const CliInvocation* invoc) {
  debug_log_tracker_init(world, g_logger);

  const String assetPath = cli_read_string(invoc, g_optAssets, string_lit("assets"));
  if (file_stat_path_sync(assetPath).type != FileType_Directory) {
    log_e("Asset directory not found", log_param("path", fmt_path(assetPath)));
    return;
  }

  const AssetManagerFlags assetFlg = AssetManagerFlags_TrackChanges | AssetManagerFlags_DelayUnload;
  AssetManagerComp*       assets   = asset_manager_create_fs(world, assetFlg, assetPath);

  GamePrefsComp* prefs      = prefs_init(world);
  const bool     fullscreen = prefs->fullscreen && !cli_parse_provided(invoc, g_optWindow);
  const u16      width      = (u16)cli_read_u64(invoc, g_optWidth, prefs->windowWidth);
  const u16      height     = (u16)cli_read_u64(invoc, g_optHeight, prefs->windowHeight);

  RendSettingsGlobalComp* rendSettingsGlobal = rend_settings_global_init(world);

  SndMixerComp* soundMixer = snd_mixer_init(world);
  snd_mixer_gain_set(soundMixer, prefs->volume * 1e-2f);

  const EcsEntityId mainWin = app_main_window_create(world, assets, fullscreen, width, height);
  RendSettingsComp* rendSettingsWin = rend_settings_window_init(world, mainWin);

  app_quality_apply(prefs, rendSettingsGlobal, rendSettingsWin);

  ecs_world_add_t(world, ecs_world_global(world), AppComp, .mainWindow = mainWin);

  InputResourceComp* inputResource = input_resource_init(world);
  input_resource_load_map(inputResource, string_lit("global/app.inputs"));
  input_resource_load_map(inputResource, string_lit("global/game.inputs"));
  input_resource_load_map(inputResource, string_lit("global/debug.inputs"));

  scene_level_load(world, asset_lookup(world, assets, g_appLevel));
  scene_prefab_init(world, string_lit("global/game.prefabs"));
  scene_weapon_init(world, string_lit("global/game.weapons"));
  scene_product_init(world, string_lit("global/game.products"));
}

bool app_ecs_should_quit(EcsWorld* world) { return !ecs_utils_any(world, MainWindowView); }
