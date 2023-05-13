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
#include "hud_internal.h"
#include "prefs_internal.h"

static const String g_appLevel = string_static("levels/default.lvl");

typedef enum {
  AppMode_Normal,
  AppMode_Debug,
} AppMode;

ecs_comp_define(AppComp) {
  AppMode     mode : 8;
  EcsEntityId mainWindow;
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

static EcsEntityId
app_window_create(EcsWorld* world, const bool fullscreen, const u16 width, const u16 height) {
  const GapVector      size   = {.width = (i32)width, .height = (i32)height};
  const GapWindowMode  mode   = fullscreen ? GapWindowMode_Fullscreen : GapWindowMode_Windowed;
  const GapWindowFlags flags  = fullscreen ? GapWindowFlags_CursorConfine : GapWindowFlags_Default;
  const EcsEntityId    window = gap_window_create(world, mode, flags, size);

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
  static const RendFlags g_rendOptionalFeatures = RendFlags_AmbientOcclusion | RendFlags_Bloom |
                                                  RendFlags_Distortion | RendFlags_ParticleShadows;
  switch (prefs->quality) {
  case GameQuality_UltraLow:
    rendSetGlobal->flags &= ~RendGlobalFlags_SunShadows;
    rendSetWin->flags &= ~g_rendOptionalFeatures;
    rendSetWin->resolutionScale = 0.75f;
    break;
  case GameQuality_Low:
    rendSetGlobal->flags |= RendGlobalFlags_SunShadows;
    rendSetWin->flags &= ~g_rendOptionalFeatures;
    rendSetWin->resolutionScale  = 0.75f;
    rendSetWin->shadowResolution = 1024;
    break;
  case GameQuality_Medium:
    rendSetGlobal->flags |= RendGlobalFlags_SunShadows;
    rendSetWin->flags |= g_rendOptionalFeatures;
    rendSetWin->resolutionScale           = 1.0f;
    rendSetWin->aoResolutionScale         = 0.75f;
    rendSetWin->shadowResolution          = 2048;
    rendSetWin->bloomSteps                = 5;
    rendSetWin->distortionResolutionScale = 0.25f;
    break;
  case GameQuality_High:
    rendSetGlobal->flags |= RendGlobalFlags_SunShadows;
    rendSetWin->flags |= g_rendOptionalFeatures;
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

    scene_level_load(ctx->world, g_appLevel);
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
  ecs_access_maybe_write(DebugStatsGlobalComp);
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
  GamePrefsComp*          prefs         = ecs_view_write_t(globalItr, GamePrefsComp);
  CmdControllerComp*      cmd           = ecs_view_write_t(globalItr, CmdControllerComp);
  RendSettingsGlobalComp* rendSetGlobal = ecs_view_write_t(globalItr, RendSettingsGlobalComp);
  SndMixerComp*           soundMixer    = ecs_view_write_t(globalItr, SndMixerComp);
  SceneTimeSettingsComp*  timeSet       = ecs_view_write_t(globalItr, SceneTimeSettingsComp);
  InputManagerComp*       input         = ecs_view_write_t(globalItr, InputManagerComp);
  DebugStatsGlobalComp*   debugStats    = ecs_view_write_t(globalItr, DebugStatsGlobalComp);

  EcsIterator* canvasItr = ecs_view_itr(ecs_world_view_t(world, UiCanvasView));

  EcsView*     windowView = ecs_world_view_t(world, WindowView);
  EcsIterator* mainWinItr = ecs_view_maybe_at(windowView, app->mainWindow);
  if (mainWinItr) {
    const EcsEntityId windowEntity    = ecs_view_entity(mainWinItr);
    AppWindowComp*    appWindow       = ecs_view_write_t(mainWinItr, AppWindowComp);
    GapWindowComp*    win             = ecs_view_write_t(mainWinItr, GapWindowComp);
    DebugStatsComp*   stats           = ecs_view_write_t(mainWinItr, DebugStatsComp);
    RendSettingsComp* rendSettingsWin = ecs_view_write_t(mainWinItr, RendSettingsComp);

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
              .rendSetWin    = rendSettingsWin,
              .debugStats    = debugStats,
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

static CliId g_assetFlag, g_windowFlag, g_widthFlag, g_heightFlag, g_helpFlag;

void app_ecs_configure(CliApp* app) {
  cli_app_register_desc(app, string_lit("Volo RTS Demo"));

  g_assetFlag = cli_register_flag(app, 'a', string_lit("assets"), CliOptionFlags_Value);
  cli_register_desc(app, g_assetFlag, string_lit("Path to asset directory."));
  cli_register_validator(app, g_assetFlag, cli_validate_file_directory);

  g_windowFlag = cli_register_flag(app, 'w', string_lit("window"), CliOptionFlags_None);
  cli_register_desc(app, g_windowFlag, string_lit("Start the game in windowed mode."));

  g_widthFlag = cli_register_flag(app, '\0', string_lit("width"), CliOptionFlags_Value);
  cli_register_desc(app, g_widthFlag, string_lit("Game window width in pixels."));
  cli_register_validator(app, g_widthFlag, cli_validate_u16);

  g_heightFlag = cli_register_flag(app, '\0', string_lit("height"), CliOptionFlags_Value);
  cli_register_desc(app, g_heightFlag, string_lit("Game window height in pixels."));
  cli_register_validator(app, g_heightFlag, cli_validate_u16);

  g_helpFlag = cli_register_flag(app, 'h', string_lit("help"), CliOptionFlags_None);
  cli_register_desc(app, g_helpFlag, string_lit("Display this help page."));
  cli_register_exclusions(app, g_helpFlag, g_assetFlag);
  cli_register_exclusions(app, g_helpFlag, g_windowFlag);
  cli_register_exclusions(app, g_helpFlag, g_widthFlag);
  cli_register_exclusions(app, g_helpFlag, g_heightFlag);
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
  const String assetPath = cli_read_string(invoc, g_assetFlag, string_lit("assets"));
  if (file_stat_path_sync(assetPath).type != FileType_Directory) {
    log_e("Asset directory not found", log_param("path", fmt_path(assetPath)));
    return;
  }

  GamePrefsComp* prefs      = prefs_init(world);
  const bool     fullscreen = prefs->fullscreen && !cli_parse_provided(invoc, g_windowFlag);
  const u16      width      = (u16)cli_read_u64(invoc, g_widthFlag, prefs->windowWidth);
  const u16      height     = (u16)cli_read_u64(invoc, g_heightFlag, prefs->windowHeight);

  RendSettingsGlobalComp* rendSettingsGlobal = rend_settings_global_init(world);

  SndMixerComp* soundMixer = snd_mixer_init(world);
  snd_mixer_gain_set(soundMixer, prefs->volume * 1e-2f);

  const EcsEntityId win             = app_window_create(world, fullscreen, width, height);
  RendSettingsComp* rendSettingsWin = rend_settings_window_init(world, win);

  app_quality_apply(prefs, rendSettingsGlobal, rendSettingsWin);

  ecs_world_add_t(world, ecs_world_global(world), AppComp, .mainWindow = win);

  const AssetManagerFlags assetFlg = AssetManagerFlags_TrackChanges | AssetManagerFlags_DelayUnload;
  AssetManagerComp*       assets   = asset_manager_create_fs(world, assetFlg, assetPath);

  app_ambiance_create(world, assets);

  InputResourceComp* inputResource = input_resource_init(world);
  input_resource_load_map(inputResource, string_lit("global/app-input.imp"));
  input_resource_load_map(inputResource, string_lit("global/game-input.imp"));
  input_resource_load_map(inputResource, string_lit("global/debug-input.imp"));

  scene_level_load(world, g_appLevel);
  scene_prefab_init(world, string_lit("global/game-prefabs.pfb"));
  scene_weapon_init(world, string_lit("global/game-weapons.wea"));
  scene_terrain_init(
      world,
      string_lit("graphics/scene/terrain.gra"),
      string_lit("external/terrain/terrain_3_height.r16"));
}

bool app_ecs_should_quit(EcsWorld* world) { return !ecs_utils_any(world, WindowView); }
