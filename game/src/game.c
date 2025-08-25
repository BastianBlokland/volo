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

#include "cmd.h"
#include "game.h"
#include "hud.h"
#include "prefs.h"

enum { GameLevelsMax = 8 };

typedef enum {
  GameMode_Normal,
  GameMode_Debug,
} GameMode;

ecs_comp_define(GameComp) {
  GameState   state : 8;
  GameMode    mode : 8;
  bool        devSupport;
  EcsEntityId mainWindow;

  u32         levelMask;
  u32         levelLoadingMask;
  EcsEntityId levelAssets[GameLevelsMax];
  String      levelNames[GameLevelsMax];
};

ecs_comp_define(GameMainWindowComp) {
  EcsEntityId uiCanvas;
  EcsEntityId devMenu;
};

static void ecs_destruct_game_comp(void* data) {
  GameComp* comp = data;
  for (u32 i = 0; i != GameLevelsMax; ++i) {
    string_maybe_free(g_allocHeap, comp->levelNames[i]);
  }
}

static EcsEntityId game_window_create(
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

  if (devSupport) {
    dev_log_viewer_create(world, window, LogMask_Info | LogMask_Warn | LogMask_Error);
  }

  const EcsEntityId uiCanvas = ui_canvas_create(world, window, UiCanvasCreateFlags_ToFront);
  ecs_world_add_t(world, window, GameMainWindowComp, .uiCanvas = uiCanvas);

  ecs_world_add_t(
      world,
      window,
      SceneCameraComp,
      .persFov   = 50 * math_deg_to_rad,
      .persNear  = 0.75f,
      .orthoSize = 5);

  ecs_world_add_empty_t(world, window, SceneSoundListenerComp);
  ecs_world_add_t(world, window, SceneTransformComp, .position = {0}, .rotation = geo_quat_ident);
  game_hud_init(world, assets, window);

  return window;
}

static void game_fullscreen_toggle(GapWindowComp* win) {
  if (gap_window_mode(win) == GapWindowMode_Fullscreen) {
    const GapVector size = gap_window_param(win, GapParam_WindowSizePreFullscreen);
    gap_window_resize(win, size, GapWindowMode_Windowed);
    gap_window_flags_unset(win, GapWindowFlags_CursorConfine);
  } else {
    gap_window_resize(win, gap_vector(0, 0), GapWindowMode_Fullscreen);
    gap_window_flags_set(win, GapWindowFlags_CursorConfine);
  }
}

static void game_quality_apply(
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
  EcsWorld*                    world;
  GameComp*                    game;
  GamePrefsComp*               prefs;
  const SceneLevelManagerComp* levelManager;
  InputManagerComp*            input;
  SndMixerComp*                soundMixer;
  SceneTimeSettingsComp*       timeSet;
  GameCmdComp*                 cmd;
  AssetManagerComp*            assets;
  SceneVisibilityEnvComp*      visibilityEnv;
  RendSettingsGlobalComp*      rendSetGlobal;

  EcsEntityId         winEntity;
  GameMainWindowComp* winGame;
  GapWindowComp*      winComp;
  RendSettingsComp*   winRendSet;
  UiCanvasComp*       winCanvas;

  EcsView* devPanelView; // Null if dev-support is not enabled.
} GameUpdateContext;

typedef void (*GameUiDrawer)(const GameUpdateContext*, u32 index);

static void game_draw_list(const GameUpdateContext* ctx, const GameUiDrawer val[], const u32 cnt) {
  static const UiVector g_entrySize = {.x = 250.0f, .y = 50.0f};
  static const f32      g_spacing   = 8.0f;

  const f32 yCenterOffset = (cnt - 1) * (g_entrySize.y + g_spacing) * 0.5f;
  ui_layout_inner(
      ctx->winCanvas, UiBase_Canvas, UiAlign_MiddleCenter, g_entrySize, UiBase_Absolute);
  ui_layout_move(ctx->winCanvas, ui_vector(g_spacing, yCenterOffset), UiBase_Absolute, Ui_XY);

  ui_style_push(ctx->winCanvas);
  ui_style_transform(ctx->winCanvas, UiTransform_ToUpper);
  for (u32 i = 0; i != cnt; ++i) {
    val[i](ctx, i);
    ui_layout_next(ctx->winCanvas, Ui_Down, g_spacing);
  }
  ui_style_pop(ctx->winCanvas);
}

static void game_draw_button_level(const GameUpdateContext* ctx, const u32 index) {
  if (ui_button(ctx->winCanvas, .label = ctx->game->levelNames[index], .fontSize = 25)) {
    scene_level_load(ctx->world, SceneLevelMode_Play, ctx->game->levelAssets[index]);
  }
}

static void game_draw_level_picker(const GameUpdateContext* ctx) {
  GameUiDrawer levelButtons[GameLevelsMax];
  const u32    levelButtonCount = bits_popcnt(ctx->game->levelMask);
  for (u32 i = 0; i != levelButtonCount; ++i) {
    levelButtons[i] = &game_draw_button_level;
  }
  game_draw_list(ctx, levelButtons, levelButtonCount);
}

static void game_draw_button_debug(const GameUpdateContext* ctx, MAYBE_UNUSED const u32 index) {
  const bool isInDebugMode = ctx->game->mode == GameMode_Debug;
  if (ui_button(
          ctx->winCanvas,
          .label      = ui_shape_scratch(UiShape_Bug),
          .fontSize   = 35,
          .tooltip    = string_lit("Enable / disable debug mode."),
          .frameColor = isInDebugMode ? ui_color(178, 0, 0, 192) : ui_color(32, 32, 32, 192),
          .activate   = input_triggered_lit(ctx->input, "Debug"))) {

    log_i("Toggle debug-mode", log_param("debug", fmt_bool(!isInDebugMode)));

    ctx->game->mode ^= GameMode_Debug;
    game_cmd_push_deselect_all(ctx->cmd);

    if (ctx->game->mode == GameMode_Debug) {
      ctx->timeSet->flags |= SceneTimeFlags_Paused;
      ctx->winRendSet->skyMode = RendSkyMode_Gradient;
    } else {
      ctx->timeSet->flags &= ~SceneTimeFlags_Paused;
      ctx->winRendSet->skyMode = RendSkyMode_None;
    }
  }
}

static void game_draw_button_pause(const GameUpdateContext* ctx, MAYBE_UNUSED const u32 index) {
  const bool isPaused = (ctx->timeSet->flags & SceneTimeFlags_Paused) != 0;
  if (ui_button(
          ctx->winCanvas,
          .label      = ui_shape_scratch(UiShape_Pause),
          .fontSize   = 35,
          .tooltip    = string_lit("Pause / Resume."),
          .frameColor = isPaused ? ui_color(0, 178, 0, 192) : ui_color(32, 32, 32, 192))) {

    log_i("Toggle pause", log_param("paused", fmt_bool(!isPaused)));
    ctx->timeSet->flags ^= SceneTimeFlags_Paused;
  }
}

static void game_draw_button_restart(const GameUpdateContext* ctx, MAYBE_UNUSED const u32 index) {
  if (ui_button(
          ctx->winCanvas,
          .label    = ui_shape_scratch(UiShape_Restart),
          .fontSize = 35,
          .tooltip  = string_lit("Restart the level."),
          .activate = input_triggered_lit(ctx->input, "Reset"))) {

    log_i("Restart");
    scene_level_reload(ctx->world, SceneLevelMode_Play);
  }
}

static void
game_draw_button_fullscreen(const GameUpdateContext* ctx, MAYBE_UNUSED const u32 index) {
  if (ui_button(
          ctx->winCanvas,
          .label    = ui_shape_scratch(UiShape_Fullscreen),
          .fontSize = 35,
          .tooltip  = string_lit("Enter / exit fullscreen."),
          .activate = input_triggered_lit(ctx->input, "WindowFullscreen"))) {

    log_i("Toggle fullscreen");
    game_fullscreen_toggle(ctx->winComp);
  }
}

static void game_draw_button_exit(const GameUpdateContext* ctx, MAYBE_UNUSED const u32 index) {
  if (ui_button(
          ctx->winCanvas,
          .label    = ui_shape_scratch(UiShape_Logout),
          .fontSize = 35,
          .tooltip  = string_lit("Close the window."),
          .activate = input_triggered_lit(ctx->input, "WindowClose"))) {
    log_i("Close window");
    gap_window_close(ctx->winComp);
  }
}

static void game_draw_settings_sound(const GameUpdateContext* ctx, MAYBE_UNUSED const u32 index) {
  static const UiVector g_popupSize    = {.x = 35.0f, .y = 100.0f};
  static const f32      g_popupSpacing = 8.0f;
  static const UiVector g_popupInset   = {.x = -15.0f, .y = -15.0f};

  const bool              muted       = ctx->prefs->volume <= f32_epsilon;
  const UiId              popupId     = ui_canvas_id_peek(ctx->winCanvas);
  const UiPersistentFlags popupFlags  = ui_canvas_persistent_flags(ctx->winCanvas, popupId);
  const bool              popupActive = (popupFlags & UiPersistentFlags_Open) != 0;

  ui_canvas_id_block_next(ctx->winCanvas);

  if (ui_button(
          ctx->winCanvas,
          .label      = ui_shape_scratch(muted ? UiShape_VolumeOff : UiShape_VolumeUp),
          .fontSize   = 35,
          .frameColor = popupActive ? ui_color(128, 128, 128, 192) : ui_color(32, 32, 32, 192),
          .tooltip    = string_lit("Open / Close the sound volume controls."))) {
    ui_canvas_persistent_flags_toggle(ctx->winCanvas, popupId, UiPersistentFlags_Open);
  }

  if (popupActive) {
    ui_layout_push(ctx->winCanvas);
    ui_layout_move(ctx->winCanvas, ui_vector(0.5f, 1.0f), UiBase_Current, Ui_XY);
    ui_layout_move_dir(ctx->winCanvas, Ui_Up, g_popupSpacing, UiBase_Absolute);
    ui_layout_resize(ctx->winCanvas, UiAlign_BottomCenter, g_popupSize, UiBase_Absolute, Ui_XY);

    // Popup background.
    ui_style_push(ctx->winCanvas);
    ui_style_outline(ctx->winCanvas, 2);
    ui_style_color(ctx->winCanvas, ui_color(128, 128, 128, 192));
    ui_canvas_draw_glyph(ctx->winCanvas, UiShape_Circle, 5, UiFlags_Interactable);
    ui_style_pop(ctx->winCanvas);

    // Volume slider.
    ui_layout_grow(ctx->winCanvas, UiAlign_MiddleCenter, g_popupInset, UiBase_Absolute, Ui_XY);
    if (ui_slider(
            ctx->winCanvas,
            &ctx->prefs->volume,
            .vertical = true,
            .max      = 1e2f,
            .step     = 1,
            .tooltip  = string_lit("Sound volume."))) {

      ctx->prefs->dirty = true;
      snd_mixer_gain_set(ctx->soundMixer, ctx->prefs->volume * 1e-2f);
    }
    ui_layout_pop(ctx->winCanvas);

    // Close when pressing outside.
    if (ui_canvas_input_any(ctx->winCanvas) && ui_canvas_group_block_inactive(ctx->winCanvas)) {
      ui_canvas_persistent_flags_unset(ctx->winCanvas, popupId, UiPersistentFlags_Open);
    }
  }

  ui_canvas_id_block_next(ctx->winCanvas); // End on an consistent id.
}

static void game_draw_settings_quality(const GameUpdateContext* ctx, MAYBE_UNUSED const u32 index) {
  static const UiVector g_popupSize    = {.x = 250.0f, .y = 70.0f};
  static const f32      g_popupSpacing = 8.0f;

  const UiId              popupId     = ui_canvas_id_peek(ctx->winCanvas);
  const UiPersistentFlags popupFlags  = ui_canvas_persistent_flags(ctx->winCanvas, popupId);
  const bool              popupActive = (popupFlags & UiPersistentFlags_Open) != 0;

  ui_canvas_id_block_next(ctx->winCanvas);

  if (ui_button(
          ctx->winCanvas,
          .label      = ui_shape_scratch(UiShape_Image),
          .fontSize   = 35,
          .frameColor = popupActive ? ui_color(128, 128, 128, 192) : ui_color(32, 32, 32, 192),
          .tooltip    = string_lit("Open / Close the quality controls."))) {
    ui_canvas_persistent_flags_toggle(ctx->winCanvas, popupId, UiPersistentFlags_Open);
  }

  if (popupActive && ctx->rendSetGlobal && ctx->winRendSet) {
    ui_layout_push(ctx->winCanvas);
    ui_layout_move(ctx->winCanvas, ui_vector(0.5f, 1.0f), UiBase_Current, Ui_XY);
    ui_layout_move_dir(ctx->winCanvas, Ui_Up, g_popupSpacing, UiBase_Absolute);
    ui_layout_resize(ctx->winCanvas, UiAlign_BottomCenter, g_popupSize, UiBase_Absolute, Ui_XY);

    // Popup background.
    ui_style_push(ctx->winCanvas);
    ui_style_outline(ctx->winCanvas, 2);
    ui_style_color(ctx->winCanvas, ui_color(128, 128, 128, 192));
    ui_canvas_draw_glyph(ctx->winCanvas, UiShape_Circle, 5, UiFlags_Interactable);
    ui_style_pop(ctx->winCanvas);

    // Settings.
    ui_layout_container_push(ctx->winCanvas, UiClip_None, UiLayer_Normal);

    UiTable table = ui_table();
    ui_table_add_column(&table, UiTableColumn_Fixed, 125);
    ui_table_add_column(&table, UiTableColumn_Fixed, 110);

    ui_table_next_row(ctx->winCanvas, &table);
    ui_label(ctx->winCanvas, string_lit("PowerSaving"));
    ui_table_next_column(ctx->winCanvas, &table);
    if (ui_toggle(ctx->winCanvas, &ctx->prefs->powerSaving)) {
      ctx->prefs->dirty = true;
      game_quality_apply(ctx->prefs, ctx->rendSetGlobal, ctx->winRendSet);
    }

    ui_table_next_row(ctx->winCanvas, &table);
    ui_label(ctx->winCanvas, string_lit("Quality"));
    ui_table_next_column(ctx->winCanvas, &table);
    i32* quality = (i32*)&ctx->prefs->quality;
    if (ui_select(ctx->winCanvas, quality, g_gameQualityLabels, GameQuality_Count)) {
      ctx->prefs->dirty = true;
      game_quality_apply(ctx->prefs, ctx->rendSetGlobal, ctx->winRendSet);
    }

    ui_layout_container_pop(ctx->winCanvas);
    ui_layout_pop(ctx->winCanvas);

    // Close when pressing outside.
    if (ui_canvas_input_any(ctx->winCanvas) && ui_canvas_group_block_inactive(ctx->winCanvas)) {
      ui_canvas_persistent_flags_unset(ctx->winCanvas, popupId, UiPersistentFlags_Open);
    }
  }

  ui_canvas_id_block_next(ctx->winCanvas); // End on an consistent id.
}

static void game_action_bar_draw(const GameUpdateContext* ctx) {
  GameUiDrawer actions[32];
  u32          actionCount = 0;

  if (ctx->game->devSupport) {
    actions[actionCount++] = game_draw_button_debug;
  }
  actions[actionCount++] = game_draw_button_pause;
  actions[actionCount++] = game_draw_button_restart;
  actions[actionCount++] = game_draw_settings_sound;
  actions[actionCount++] = game_draw_settings_quality;
  actions[actionCount++] = game_draw_button_fullscreen;
  actions[actionCount++] = game_draw_button_exit;

  static const UiVector g_buttonSize = {.x = 50.0f, .y = 50.0f};
  static const f32      g_spacing    = 8.0f;

  const f32 xCenterOffset = (actionCount - 1) * (g_buttonSize.x + g_spacing) * -0.5f;
  ui_layout_inner(
      ctx->winCanvas, UiBase_Canvas, UiAlign_BottomCenter, g_buttonSize, UiBase_Absolute);
  ui_layout_move(ctx->winCanvas, ui_vector(xCenterOffset, g_spacing), UiBase_Absolute, Ui_XY);

  for (u32 i = 0; i != actionCount; ++i) {
    actions[i](ctx, i);
    ui_layout_next(ctx->winCanvas, Ui_Right, g_spacing);
  }
}

ecs_view_define(ErrorView) {
  ecs_access_maybe_read(GapErrorComp);
  ecs_access_maybe_read(RendErrorComp);
}
ecs_view_define(TimeView) { ecs_access_write(SceneTimeComp); }

ecs_view_define(UpdateGlobalView) {
  ecs_access_read(SceneLevelManagerComp);
  ecs_access_write(GameComp);
  ecs_access_write(AssetManagerComp);
  ecs_access_write(GameCmdComp);
  ecs_access_write(GamePrefsComp);
  ecs_access_write(InputManagerComp);
  ecs_access_write(RendSettingsGlobalComp);
  ecs_access_write(SceneTimeSettingsComp);
  ecs_access_write(SceneVisibilityEnvComp);
  ecs_access_write(SndMixerComp);
}

ecs_view_define(MainWindowView) {
  ecs_access_maybe_write(RendSettingsComp);
  ecs_access_write(GameMainWindowComp);
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

static void game_levels_query_init(EcsWorld* world, GameComp* game, AssetManagerComp* assets) {
  const String levelPattern = string_lit("levels/game/*.level");
  EcsEntityId  queryAssets[asset_query_max_results];
  const u32    queryCount = asset_query(world, assets, levelPattern, queryAssets);

  for (u32 i = 0; i != math_min(queryCount, GameLevelsMax); ++i) {
    asset_acquire(world, queryAssets[i]);
    game->levelLoadingMask |= 1 << i;
    game->levelAssets[i] = queryAssets[i];
  }
}

static void game_levels_query_update(const GameUpdateContext* ctx) {
  if (!ctx->game->levelLoadingMask) {
    return; // Loading finished.
  }
  EcsIterator* levelItr = ecs_view_itr(ecs_world_view_t(ctx->world, LevelView));
  bitset_for(bitset_from_var(ctx->game->levelLoadingMask), idx) {
    const EcsEntityId asset = ctx->game->levelAssets[idx];
    if (UNLIKELY(ecs_world_has_t(ctx->world, asset, AssetFailedComp))) {
      goto Done;
    }
    if (!ecs_world_has_t(ctx->world, asset, AssetLoadedComp)) {
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
    ctx->game->levelMask |= 1 << idx;
    ctx->game->levelNames[idx] = string_dup(g_allocHeap, name);
  Done:
    asset_release(ctx->world, asset);
    ctx->game->levelLoadingMask &= ~(1 << idx);
  }
}

static void game_dev_panels_hide(const GameUpdateContext* ctx, const bool hidden) {
  if (!ctx->devPanelView) {
    return; // Dev support not enabled.
  }
  for (EcsIterator* itr = ecs_view_itr(ctx->devPanelView); ecs_view_walk(itr);) {
    DevPanelComp* panel = ecs_view_write_t(itr, DevPanelComp);
    if (dev_panel_type(panel) != DevPanelType_Detached) {
      dev_panel_hide(panel, hidden);
    }
  }
}

ecs_system_define(GameUpdateSys) {
  EcsView*     globalView = ecs_world_view_t(world, UpdateGlobalView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (!globalItr) {
    return;
  }

  GameUpdateContext ctx = {
      .world         = world,
      .game          = ecs_view_write_t(globalItr, GameComp),
      .prefs         = ecs_view_write_t(globalItr, GamePrefsComp),
      .levelManager  = ecs_view_read_t(globalItr, SceneLevelManagerComp),
      .input         = ecs_view_write_t(globalItr, InputManagerComp),
      .soundMixer    = ecs_view_write_t(globalItr, SndMixerComp),
      .timeSet       = ecs_view_write_t(globalItr, SceneTimeSettingsComp),
      .cmd           = ecs_view_write_t(globalItr, GameCmdComp),
      .assets        = ecs_view_write_t(globalItr, AssetManagerComp),
      .visibilityEnv = ecs_view_write_t(globalItr, SceneVisibilityEnvComp),
      .rendSetGlobal = ecs_view_write_t(globalItr, RendSettingsGlobalComp),
      .devPanelView  = ecs_world_view_t(world, DevPanelView),
  };

  game_levels_query_update(&ctx);

  if (scene_level_loaded(ctx.levelManager)) {
    asset_loading_budget_set(ctx.assets, time_milliseconds(2)); // Limit loading during gameplay.
  } else {
    asset_loading_budget_set(ctx.assets, 0); // Infinite while not in gameplay.
  }

  EcsIterator* canvasItr   = ecs_view_itr(ecs_world_view_t(world, UiCanvasView));
  EcsView*     mainWinView = ecs_world_view_t(world, MainWindowView);
  EcsIterator* mainWinItr  = ecs_view_maybe_at(mainWinView, ctx.game->mainWindow);
  if (mainWinItr) {
    ctx.winEntity  = ecs_view_entity(mainWinItr);
    ctx.winGame    = ecs_view_write_t(mainWinItr, GameMainWindowComp);
    ctx.winComp    = ecs_view_write_t(mainWinItr, GapWindowComp);
    ctx.winRendSet = ecs_view_write_t(mainWinItr, RendSettingsComp);

    // Save last window size.
    if (gap_window_events(ctx.winComp) & GapWindowEvents_Resized) {
      ctx.prefs->fullscreen = gap_window_mode(ctx.winComp) == GapWindowMode_Fullscreen;
      if (!ctx.prefs->fullscreen) {
        ctx.prefs->windowWidth  = gap_window_param(ctx.winComp, GapParam_WindowSize).width;
        ctx.prefs->windowHeight = gap_window_param(ctx.winComp, GapParam_WindowSize).height;
      }
      ctx.prefs->dirty = true;
    }

    if (ecs_view_maybe_jump(canvasItr, ctx.winGame->uiCanvas)) {
      ctx.winCanvas = ecs_view_write_t(canvasItr, UiCanvasComp);
      ui_canvas_reset(ctx.winCanvas);
      if (!scene_level_loaded(ctx.levelManager)) {
        game_draw_level_picker(&ctx);
      }
      game_action_bar_draw(&ctx);
    }

    // clang-format off
    switch (ctx.game->mode) {
    case GameMode_Normal:
      game_dev_panels_hide(&ctx, true);
      input_layer_disable(ctx.input, string_hash_lit("Dev"));
      input_layer_enable(ctx.input, string_hash_lit("Game"));
      scene_visibility_flags_clear(ctx.visibilityEnv, SceneVisibilityFlags_ForceRender);
      break;
    case GameMode_Debug:
      if (!ctx.winGame->devMenu) { ctx.winGame->devMenu = dev_menu_create(world, ctx.winEntity); }
      game_dev_panels_hide(&ctx, false);
      input_layer_enable(ctx.input, string_hash_lit("Dev"));
      input_layer_disable(ctx.input, string_hash_lit("Game"));
      scene_visibility_flags_set(ctx.visibilityEnv, SceneVisibilityFlags_ForceRender);
      break;
    }
    // clang-format on
  }
}

typedef struct {
  bool devSupport;
} GameInitContext;

ecs_module_init(game_module) {
  const GameInitContext* ctx = ecs_init_ctx();

  ecs_register_comp(GameComp, .destructor = ecs_destruct_game_comp);
  ecs_register_comp(GameMainWindowComp);

  ecs_register_view(TimeView);
  ecs_register_view(ErrorView);
  ecs_register_view(UpdateGlobalView);
  ecs_register_view(MainWindowView);
  ecs_register_view(LevelView);
  ecs_register_view(UiCanvasView);

  if (ctx->devSupport) {
    ecs_register_view(DevPanelView);
  }

  ecs_register_system(
      GameUpdateSys,
      ecs_view_id(UpdateGlobalView),
      ecs_view_id(MainWindowView),
      ecs_view_id(LevelView),
      ecs_view_id(UiCanvasView),
      ecs_view_id(DevPanelView));
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

  const GameInitContext gameInitCtx = {
      .devSupport = cli_parse_provided(invoc, g_optDev),
  };

  asset_register(def);
  gap_register(def);
  input_register(def);
  rend_register(def, gameInitCtx.devSupport ? RendRegisterFlags_EnableStats : 0);
  scene_register(def);
  snd_register(def);
  ui_register(def);
  vfx_register(def);
  if (gameInitCtx.devSupport) {
    dev_register(def);
  }

  ecs_register_module_with_context(def, game_module, &gameInitCtx);
  ecs_register_module(def, game_cmd_module);
  ecs_register_module(def, game_hud_module);
  ecs_register_module(def, game_input_module);
  ecs_register_module(def, game_prefs_module);
}

static AssetManagerComp* game_init_assets(EcsWorld* world, const CliInvocation* invoc) {
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

  AssetManagerComp* assets = game_init_assets(world, invoc);
  if (UNLIKELY(!assets)) {
    gap_window_modal_error(string_lit("No (valid) assets found"));
    return false; // Initialization failed.
  }
  GamePrefsComp* prefs      = game_prefs_init(world);
  const bool     fullscreen = prefs->fullscreen && !cli_parse_provided(invoc, g_optWindow);
  const u16      width      = (u16)cli_read_u64(invoc, g_optWidth, prefs->windowWidth);
  const u16      height     = (u16)cli_read_u64(invoc, g_optHeight, prefs->windowHeight);

  RendSettingsGlobalComp* rendSettingsGlobal = rend_settings_global_init(world, devSupport);

  SndMixerComp* soundMixer = snd_mixer_init(world);
  snd_mixer_gain_set(soundMixer, prefs->volume * 1e-2f);

  const EcsEntityId mainWin =
      game_window_create(world, assets, fullscreen, devSupport, width, height);
  RendSettingsComp* rendSettingsWin = rend_settings_window_init(world, mainWin);

  game_quality_apply(prefs, rendSettingsGlobal, rendSettingsWin);

  GameComp* game = ecs_world_add_t(
      world, ecs_world_global(world), GameComp, .devSupport = devSupport, .mainWindow = mainWin);

  game_levels_query_init(world, game, assets);

  InputResourceComp* inputResource = input_resource_init(world);
  input_resource_load_map(inputResource, string_lit("global/global.inputs"));
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
  EcsView*            errView    = ecs_world_view_t(world, ErrorView);
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
   * Run until the main window has closed.
   */
  if (!ecs_utils_any(world, MainWindowView)) {
    return AppEcsStatus_Finished;
  }
  return AppEcsStatus_Running;
}

void app_ecs_set_frame(EcsWorld* world, const u64 frameIdx) {
  SceneTimeComp* time = ecs_utils_write_first_t(world, TimeView, SceneTimeComp);
  if (LIKELY(time)) {
    time->frameIdx = frameIdx;
  }
}

GameState game_state(const GameComp* game) { return game->state; }
