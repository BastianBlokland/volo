#include "app/ecs.h"
#include "asset/manager.h"
#include "asset/register.h"
#include "cli/app.h"
#include "cli/parse.h"
#include "cli/read.h"
#include "cli/validate.h"
#include "core/alloc.h"
#include "core/diag.h"
#include "core/file.h"
#include "core/float.h"
#include "core/math.h"
#include "core/path.h"
#include "core/version.h"
#include "dev/log_viewer.h"
#include "dev/menu.h"
#include "dev/panel.h"
#include "dev/register.h"
#include "dev/stats.h"
#include "ecs/entity.h"
#include "ecs/utils.h"
#include "ecs/view.h"
#include "ecs/world.h"
#include "gap/error.h"
#include "gap/register.h"
#include "gap/vector.h"
#include "gap/window.h"
#include "input/manager.h"
#include "input/register.h"
#include "input/resource.h"
#include "log/logger.h"
#include "rend/error.h"
#include "rend/register.h"
#include "rend/settings.h"
#include "scene/camera.h"
#include "scene/level.h"
#include "scene/prefab.h"
#include "scene/product.h"
#include "scene/register.h"
#include "scene/time.h"
#include "scene/transform.h"
#include "scene/visibility.h"
#include "scene/weapon.h"
#include "snd/mixer.h"
#include "snd/register.h"
#include "ui/canvas.h"
#include "ui/layout.h"
#include "ui/register.h"
#include "ui/shape.h"
#include "ui/style.h"
#include "ui/table.h"
#include "ui/widget.h"
#include "vfx/register.h"

#include "app.h"
#include "cmd.h"
#include "hud.h"
#include "prefs.h"

enum { AppLevelsMax = 8 };

typedef enum {
  AppMode_Normal,
  AppMode_Debug,
} AppMode;

ecs_comp_define(AppComp) {
  AppMode     mode : 8;
  AppState    state : 8;
  bool        devSupport;
  EcsEntityId mainWindow;

  u32         levelMask;
  u32         levelLoadingMask;
  EcsEntityId levelAssets[AppLevelsMax];
  String      levelNames[AppLevelsMax];
};

ecs_comp_define(AppMainWindowComp) {
  EcsEntityId uiCanvas;
  EcsEntityId devMenu;
  EcsEntityId devLogViewer;
};

static void ecs_destruct_app_comp(void* data) {
  AppComp* comp = data;
  for (u32 i = 0; i != AppLevelsMax; ++i) {
    string_maybe_free(g_allocHeap, comp->levelNames[i]);
  }
}

static EcsEntityId app_main_window_create(
    EcsWorld*         world,
    AssetManagerComp* assets,
    const bool        fullscreen,
    const bool        devSupport,
    const u16         width,
    const u16         height) {
  GapWindowFlags flags = GapWindowFlags_Default;
  flags |= fullscreen ? GapWindowFlags_CursorConfine : 0;

  const GapVector     size = {.width = (i32)width, .height = (i32)height};
  const GapWindowMode mode = fullscreen ? GapWindowMode_Fullscreen : GapWindowMode_Windowed;
  const GapIcon       icon = GapIcon_Main;
  const String        versionScratch = version_str_scratch(g_versionExecutable);
  const String        titleScratch   = fmt_write_scratch("Volo v{}", fmt_text(versionScratch));
  const EcsEntityId   window = gap_window_create(world, mode, flags, size, icon, titleScratch);

  const EcsEntityId uiCanvas  = ui_canvas_create(world, window, UiCanvasCreateFlags_ToFront);
  EcsEntityId       logViewer = ecs_entity_invalid;
  if (devSupport) {
    logViewer = dev_log_viewer_create(world, window, LogMask_None);
  }
  ecs_world_add_t(
      world, window, AppMainWindowComp, .uiCanvas = uiCanvas, .devLogViewer = logViewer);

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

static void app_level_picker_draw(UiCanvasComp* canvas, EcsWorld* world, AppComp* app) {
  static const UiVector g_buttonSize = {.x = 250.0f, .y = 50.0f};
  static const f32      g_spacing    = 8.0f;

  const u32 levelCount    = bits_popcnt(app->levelMask);
  const f32 yCenterOffset = (levelCount - 1) * (g_buttonSize.y + g_spacing) * 0.5f;
  ui_layout_inner(canvas, UiBase_Canvas, UiAlign_MiddleCenter, g_buttonSize, UiBase_Absolute);
  ui_layout_move(canvas, ui_vector(g_spacing, yCenterOffset), UiBase_Absolute, Ui_XY);

  ui_style_push(canvas);
  ui_style_transform(canvas, UiTransform_ToUpper);
  bitset_for(bitset_from_var(app->levelMask), idx) {
    if (ui_button(canvas, .label = app->levelNames[idx], .fontSize = 25)) {
      scene_level_load(world, SceneLevelMode_Play, app->levelAssets[idx]);
    }
    ui_layout_next(canvas, Ui_Down, g_spacing);
  }
  ui_style_pop(canvas);
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
  DevStatsGlobalComp*     devStats;
} AppActionContext;

static void app_action_notify(const AppActionContext* ctx, const String action) {
  if (ctx->devStats) {
    dev_stats_notify(ctx->devStats, string_lit("Action"), action);
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

    scene_level_reload(ctx->world, SceneLevelMode_Play);
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
    ui_layout_container_push(canvas, UiClip_None, UiLayer_Normal);

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
    if (ui_select(canvas, quality, g_gameQualityLabels, GameQuality_Count)) {
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
  void (*actions[32])(UiCanvasComp*, const AppActionContext*);
  u32 actionCount = 0;

  if (ctx->app->devSupport) {
    actions[actionCount++] = app_action_debug_draw;
  }
  actions[actionCount++] = app_action_pause_draw;
  actions[actionCount++] = app_action_restart_draw;
  actions[actionCount++] = app_action_sound_draw;
  actions[actionCount++] = app_action_quality_draw;
  actions[actionCount++] = app_action_fullscreen_draw;
  actions[actionCount++] = app_action_exit_draw;

  static const UiVector g_buttonSize = {.x = 50.0f, .y = 50.0f};
  static const f32      g_spacing    = 8.0f;

  const f32 xCenterOffset = (actionCount - 1) * (g_buttonSize.x + g_spacing) * -0.5f;
  ui_layout_inner(canvas, UiBase_Canvas, UiAlign_BottomCenter, g_buttonSize, UiBase_Absolute);
  ui_layout_move(canvas, ui_vector(xCenterOffset, g_spacing), UiBase_Absolute, Ui_XY);

  for (u32 i = 0; i != actionCount; ++i) {
    actions[i](canvas, ctx);
    ui_layout_next(canvas, Ui_Right, g_spacing);
  }
}

ecs_view_define(AppErrorView) {
  ecs_access_maybe_read(GapErrorComp);
  ecs_access_maybe_read(RendErrorComp);
}
ecs_view_define(AppTimeView) { ecs_access_write(SceneTimeComp); }

ecs_view_define(AppUpdateGlobalView) {
  ecs_access_read(SceneLevelManagerComp);
  ecs_access_write(AppComp);
  ecs_access_write(AssetManagerComp);
  ecs_access_write(CmdControllerComp);
  ecs_access_write(GamePrefsComp);
  ecs_access_write(InputManagerComp);
  ecs_access_write(RendSettingsGlobalComp);
  ecs_access_write(SceneTimeSettingsComp);
  ecs_access_write(SceneVisibilityEnvComp);
  ecs_access_write(SndMixerComp);
  ecs_access_maybe_write(DevStatsGlobalComp);
}

ecs_view_define(MainWindowView) {
  ecs_access_maybe_write(RendSettingsComp);
  ecs_access_write(AppMainWindowComp);
  ecs_access_write(GapWindowComp);
}

ecs_view_define(LevelView) {
  ecs_access_read(AssetComp);
  ecs_access_read(AssetLevelComp);
}

ecs_view_define(UiCanvasView) {
  ecs_view_flags(EcsViewFlags_Exclusive); // Only access the canvas's we create.
  ecs_access_write(UiCanvasComp);
}

ecs_view_define(DevPanelView) { ecs_access_write(DevPanelComp); }
ecs_view_define(DevLogViewerView) { ecs_access_write(DevLogViewerComp); }

static void app_levels_query_init(EcsWorld* world, AppComp* app, AssetManagerComp* assets) {
  const String levelPattern = string_lit("levels/game/*.level");
  EcsEntityId  queryAssets[asset_query_max_results];
  const u32    queryCount = asset_query(world, assets, levelPattern, queryAssets);

  for (u32 i = 0; i != math_min(queryCount, AppLevelsMax); ++i) {
    asset_acquire(world, queryAssets[i]);
    app->levelLoadingMask |= 1 << i;
    app->levelAssets[i] = queryAssets[i];
  }
}

static void app_levels_query_update(EcsWorld* world, AppComp* app) {
  if (!app->levelLoadingMask) {
    return; // Loading finished.
  }
  EcsIterator* levelItr = ecs_view_itr(ecs_world_view_t(world, LevelView));
  bitset_for(bitset_from_var(app->levelLoadingMask), idx) {
    const EcsEntityId asset = app->levelAssets[idx];
    if (UNLIKELY(ecs_world_has_t(world, asset, AssetFailedComp))) {
      goto Done;
    }
    if (!ecs_world_has_t(world, asset, AssetLoadedComp)) {
      continue; // Still loading.
    }
    if (UNLIKELY(!ecs_view_maybe_jump(levelItr, asset))) {
      log_e("Invalid level", log_param("entity", ecs_entity_fmt(asset)));
      goto Done;
    }
    String name = ecs_view_read_t(levelItr, AssetLevelComp)->level.name;
    if (string_is_empty(name)) {
      name = path_stem(asset_id(ecs_view_read_t(levelItr, AssetComp)));
    }
    app->levelMask |= 1 << idx;
    app->levelNames[idx] = string_dup(g_allocHeap, name);
  Done:
    asset_release(world, asset);
    app->levelLoadingMask &= ~(1 << idx);
  }
}

static void app_dev_hide(EcsWorld* world, const bool hidden) {
  EcsView* devPanelView = ecs_world_view_t(world, DevPanelView);
  if (!devPanelView) {
    return; // Dev support not enabled.
  }
  for (EcsIterator* itr = ecs_view_itr(devPanelView); ecs_view_walk(itr);) {
    DevPanelComp* panel = ecs_view_write_t(itr, DevPanelComp);
    if (dev_panel_type(panel) != DevPanelType_Detached) {
      dev_panel_hide(panel, hidden);
    }
  }
}

ecs_system_define(AppUpdateSys) {
  EcsView*     globalView = ecs_world_view_t(world, AppUpdateGlobalView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (!globalItr) {
    return;
  }
  const SceneLevelManagerComp* levelManager  = ecs_view_read_t(globalItr, SceneLevelManagerComp);
  AppComp*                     app           = ecs_view_write_t(globalItr, AppComp);
  AssetManagerComp*            assets        = ecs_view_write_t(globalItr, AssetManagerComp);
  CmdControllerComp*           cmd           = ecs_view_write_t(globalItr, CmdControllerComp);
  DevStatsGlobalComp*          devStats      = ecs_view_write_t(globalItr, DevStatsGlobalComp);
  GamePrefsComp*               prefs         = ecs_view_write_t(globalItr, GamePrefsComp);
  InputManagerComp*            input         = ecs_view_write_t(globalItr, InputManagerComp);
  RendSettingsGlobalComp*      rendSetGlobal = ecs_view_write_t(globalItr, RendSettingsGlobalComp);
  SceneTimeSettingsComp*       timeSet       = ecs_view_write_t(globalItr, SceneTimeSettingsComp);
  SceneVisibilityEnvComp*      visibilityEnv = ecs_view_write_t(globalItr, SceneVisibilityEnvComp);
  SndMixerComp*                soundMixer    = ecs_view_write_t(globalItr, SndMixerComp);

  app_levels_query_update(world, app);

  if (scene_level_loaded(levelManager)) {
    asset_loading_budget_set(assets, time_milliseconds(2)); // Limit asset loading during gameplay.
  } else {
    asset_loading_budget_set(assets, 0); // Infinite while not in gameplay.
  }

  EcsIterator* canvasItr        = ecs_view_itr(ecs_world_view_t(world, UiCanvasView));
  EcsIterator* devLogViewerItr  = null;
  EcsView*     devLogViewerView = ecs_world_view_t(world, DevLogViewerView);
  if (devLogViewerView) {
    devLogViewerItr = ecs_view_itr(devLogViewerView);
  }

  EcsView*     mainWinView = ecs_world_view_t(world, MainWindowView);
  EcsIterator* mainWinItr  = ecs_view_maybe_at(mainWinView, app->mainWindow);
  if (mainWinItr) {
    const EcsEntityId  windowEntity = ecs_view_entity(mainWinItr);
    AppMainWindowComp* appWindow    = ecs_view_write_t(mainWinItr, AppMainWindowComp);
    GapWindowComp*     win          = ecs_view_write_t(mainWinItr, GapWindowComp);
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
      if (!scene_level_loaded(levelManager)) {
        app_level_picker_draw(canvas, world, app);
      }
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
              .devStats      = devStats,
          });
    }

    DevLogViewerComp* devLogViewer = null;
    if (devLogViewerItr && ecs_view_maybe_jump(devLogViewerItr, appWindow->devLogViewer)) {
      devLogViewer = ecs_view_write_t(devLogViewerItr, DevLogViewerComp);
    }

    // clang-format off
    switch (app->mode) {
    case AppMode_Normal:
      if (devLogViewer) { dev_log_viewer_set_mask(devLogViewer, LogMask_Warn | LogMask_Error); }
      app_dev_hide(world, true);
      input_layer_disable(input, string_hash_lit("Dev"));
      input_layer_enable(input, string_hash_lit("Game"));
      scene_visibility_flags_clear(visibilityEnv, SceneVisibilityFlags_ForceRender);
      break;
    case AppMode_Debug:
      if (!appWindow->devMenu) { appWindow->devMenu = dev_menu_create(world, windowEntity); }
      if (devLogViewer)        { dev_log_viewer_set_mask(devLogViewer, LogMask_All); }
      app_dev_hide(world, false);
      input_layer_enable(input, string_hash_lit("Dev"));
      input_layer_disable(input, string_hash_lit("Game"));
      scene_visibility_flags_set(visibilityEnv, SceneVisibilityFlags_ForceRender);
      break;
    }
    // clang-format on
  }
}

typedef struct {
  bool devSupport;
} AppInitContext;

ecs_module_init(game_app_module) {
  const AppInitContext* ctx = ecs_init_ctx();

  ecs_register_comp(AppComp, .destructor = ecs_destruct_app_comp);
  ecs_register_comp(AppMainWindowComp);

  ecs_register_view(AppTimeView);
  ecs_register_view(AppErrorView);
  ecs_register_view(AppUpdateGlobalView);
  ecs_register_view(MainWindowView);
  ecs_register_view(LevelView);
  ecs_register_view(UiCanvasView);

  if (ctx->devSupport) {
    ecs_register_view(DevPanelView);
    ecs_register_view(DevLogViewerView);
  }

  ecs_register_system(
      AppUpdateSys,
      ecs_view_id(AppUpdateGlobalView),
      ecs_view_id(MainWindowView),
      ecs_view_id(LevelView),
      ecs_view_id(UiCanvasView),
      ecs_view_id(DevPanelView),
      ecs_view_id(DevLogViewerView));
}

static CliId g_optAssets, g_optWindow, g_optWidth, g_optHeight, g_optLevel, g_optDev;

AppType app_ecs_configure(CliApp* app) {
  cli_app_register_desc(app, string_lit("Volo RTS Demo"));

  g_optAssets = cli_register_flag(app, 'a', string_lit("assets"), CliOptionFlags_Value);
  cli_register_desc(app, g_optAssets, string_lit("Path to asset directory / pack file."));
  cli_register_validator(app, g_optAssets, cli_validate_file);

  g_optWindow = cli_register_flag(app, 'w', string_lit("window"), CliOptionFlags_None);
  cli_register_desc(app, g_optWindow, string_lit("Start the game in windowed mode."));

  g_optWidth = cli_register_flag(app, '\0', string_lit("width"), CliOptionFlags_Value);
  cli_register_desc(app, g_optWidth, string_lit("Game window width in pixels."));
  cli_register_validator(app, g_optWidth, cli_validate_u16);

  g_optHeight = cli_register_flag(app, '\0', string_lit("height"), CliOptionFlags_Value);
  cli_register_desc(app, g_optHeight, string_lit("Game window height in pixels."));
  cli_register_validator(app, g_optHeight, cli_validate_u16);

  g_optLevel = cli_register_flag(app, 'l', string_lit("level"), CliOptionFlags_Value);
  cli_register_desc(app, g_optLevel, string_lit("Level to load."));

  g_optDev = cli_register_flag(app, 'd', string_lit("dev"), CliOptionFlags_None);
  cli_register_desc(app, g_optDev, string_lit("Enable development mode."));

  return AppType_Gui;
}

static void game_crash_handler(const String message, void* ctx) {
  (void)ctx;
  /**
   * Application has crashed.
   * NOTE: Crashes are always fatal, this handler cannot prevent application shutdown. Care must be
   * taken while writing this handler as the application is in an unknown state.
   */
  gap_window_modal_error(message);
}

void app_ecs_register(EcsDef* def, const CliInvocation* invoc) {
  diag_crash_handler(game_crash_handler, null); // Register a crash handler.

  const AppInitContext appInitCtx = {
      .devSupport = cli_parse_provided(invoc, g_optDev),
  };

  asset_register(def);
  gap_register(def);
  input_register(def);
  rend_register(def, appInitCtx.devSupport ? RendRegisterFlags_EnableStats : 0);
  scene_register(def);
  snd_register(def);
  ui_register(def);
  vfx_register(def);
  if (appInitCtx.devSupport) {
    dev_register(def);
  }

  ecs_register_module_with_context(def, game_app_module, &appInitCtx);
  ecs_register_module(def, game_cmd_module);
  ecs_register_module(def, game_hud_module);
  ecs_register_module(def, game_input_module);
  ecs_register_module(def, game_prefs_module);
}

static AssetManagerComp* app_init_assets(EcsWorld* world, const CliInvocation* invoc) {
  const AssetManagerFlags flags        = AssetManagerFlags_DelayUnload;
  const String            overridePath = cli_read_string(invoc, g_optAssets, string_empty);
  if (!string_is_empty(overridePath)) {
    const FileInfo overrideInfo = file_stat_path_sync(overridePath);
    switch (overrideInfo.type) {
    case FileType_Regular:
      return asset_manager_create_pack(world, flags, overridePath);
    case FileType_Directory:
      return asset_manager_create_fs(world, flags | AssetManagerFlags_TrackChanges, overridePath);
    default:
      log_e("Asset directory / pack file not found", log_param("path", fmt_path(overridePath)));
      return null;
    }
  }
  const String pathPackDefault = string_lit("assets.blob");
  if (file_stat_path_sync(pathPackDefault).type == FileType_Regular) {
    return asset_manager_create_pack(world, flags, pathPackDefault);
  }
  const String pathFsDefault = string_lit("assets");
  if (file_stat_path_sync(pathFsDefault).type == FileType_Directory) {
    return asset_manager_create_fs(world, flags | AssetManagerFlags_TrackChanges, pathFsDefault);
  }
  log_e("No asset source found");
  return null;
}

bool app_ecs_init(EcsWorld* world, const CliInvocation* invoc) {
  const bool devSupport = cli_parse_provided(invoc, g_optDev);
  if (devSupport) {
    dev_log_tracker_init(world, g_logger);
    log_i("Development support enabled");
  }

  AssetManagerComp* assets = app_init_assets(world, invoc);
  if (UNLIKELY(!assets)) {
    gap_window_modal_error(string_lit("No (valid) assets found"));
    return false; // Initialization failed.
  }
  GamePrefsComp* prefs      = prefs_init(world);
  const bool     fullscreen = prefs->fullscreen && !cli_parse_provided(invoc, g_optWindow);
  const u16      width      = (u16)cli_read_u64(invoc, g_optWidth, prefs->windowWidth);
  const u16      height     = (u16)cli_read_u64(invoc, g_optHeight, prefs->windowHeight);

  RendSettingsGlobalComp* rendSettingsGlobal = rend_settings_global_init(world, devSupport);

  SndMixerComp* soundMixer = snd_mixer_init(world);
  snd_mixer_gain_set(soundMixer, prefs->volume * 1e-2f);

  const EcsEntityId mainWin =
      app_main_window_create(world, assets, fullscreen, devSupport, width, height);
  RendSettingsComp* rendSettingsWin = rend_settings_window_init(world, mainWin);

  app_quality_apply(prefs, rendSettingsGlobal, rendSettingsWin);

  AppComp* app = ecs_world_add_t(
      world, ecs_world_global(world), AppComp, .devSupport = devSupport, .mainWindow = mainWin);

  app_levels_query_init(world, app, assets);

  InputResourceComp* inputResource = input_resource_init(world);
  input_resource_load_map(inputResource, string_lit("global/app.inputs"));
  input_resource_load_map(inputResource, string_lit("global/game.inputs"));
  if (devSupport) {
    input_resource_load_map(inputResource, string_lit("global/dev.inputs"));
  }

  scene_prefab_init(world, string_lit("global/game.prefabs"));
  scene_weapon_init(world, string_lit("global/game.weapons"));
  scene_product_init(world, string_lit("global/game.products"));

  const String level = cli_read_string(invoc, g_optLevel, string_empty);
  if (!string_is_empty(level)) {
    scene_level_load(world, SceneLevelMode_Play, asset_lookup(world, assets, level));
  }

  return true; // Initialization succeeded.
}

AppEcsStatus app_ecs_status(EcsWorld* world) {
  /**
   * Detect any fatal errors.
   */
  EcsView*            errView    = ecs_world_view_t(world, AppErrorView);
  EcsIterator*        errItr     = ecs_view_at(errView, ecs_world_global(world));
  const GapErrorComp* errGapComp = ecs_view_read_t(errItr, GapErrorComp);
  if (errGapComp) {
    log_e("Fatal platform error", log_param("error", fmt_text(gap_error_str(errGapComp->type))));
    gap_window_modal_error(gap_error_str(errGapComp->type));
    return AppEcsStatus_Failed;
  }
  const RendErrorComp* errRendComp = ecs_view_read_t(errItr, RendErrorComp);
  if (errRendComp) {
    log_e("Fatal renderer error", log_param("error", fmt_text(rend_error_str(errRendComp->type))));
    gap_window_modal_error(rend_error_str(errRendComp->type));
    return AppEcsStatus_Failed;
  }
  /**
   * Run until the last window has been closed.
   */
  if (!ecs_utils_any(world, MainWindowView)) {
    return AppEcsStatus_Finished;
  }
  return AppEcsStatus_Running;
}

void app_ecs_set_frame(EcsWorld* world, const u64 frameIdx) {
  SceneTimeComp* time = ecs_utils_write_first_t(world, AppTimeView, SceneTimeComp);
  if (LIKELY(time)) {
    time->frameIdx = frameIdx;
  }
}

AppState app_state(const AppComp* app) { return app->state; }
