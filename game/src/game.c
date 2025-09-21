#include "app/ecs.h"
#include "asset/manager.h"
#include "asset/raw.h"
#include "asset/register.h"
#include "cli/app.h"
#include "cli/parse.h"
#include "cli/read.h"
#include "cli/validate.h"
#include "core/alloc.h"
#include "core/bitset.h"
#include "core/diag.h"
#include "core/file.h"
#include "core/math.h"
#include "core/rng.h"
#include "core/version.h"
#include "dev/level.h"
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
#include "loc/manager.h"
#include "loc/register.h"
#include "loc/translate.h"
#include "log/logger.h"
#include "rend/error.h"
#include "rend/forward.h"
#include "rend/register.h"
#include "rend/settings.h"
#include "scene/camera.h"
#include "scene/faction.h"
#include "scene/level.h"
#include "scene/mission.h"
#include "scene/prefab.h"
#include "scene/product.h"
#include "scene/register.h"
#include "scene/renderable.h"
#include "scene/terrain.h"
#include "scene/time.h"
#include "scene/transform.h"
#include "scene/visibility.h"
#include "scene/weapon.h"
#include "snd/mixer.h"
#include "snd/register.h"
#include "ui/canvas.h"
#include "ui/layout.h"
#include "ui/register.h"
#include "ui/scrollview.h"
#include "ui/settings.h"
#include "ui/shape.h"
#include "ui/style.h"
#include "ui/widget.h"
#include "vfx/register.h"

#include "cmd.h"
#include "game.h"
#include "hud.h"
#include "id.h"
#include "input.h"
#include "prefs.h"

enum {
  GameLevelsMax       = 8,
  GameLoadingMinTicks = 5, // Not strictly needed, but avoids very short loading screen flashes.
};

typedef enum {
  GameFlags_None          = 0,
  GameFlags_DevSupport    = 1 << 0,
  GameFlags_DebugActive   = 1 << 1,
  GameFlags_EditMode      = 1 << 2,
  GameFlags_RefreshLevels = 1 << 3,
} GameFlags;

ecs_comp_define(GameComp) {
  GameState state : 8;
  GameState statePrev : 8;
  GameState stateNext : 8;
  GameFlags flags : 8;
  u32       stateTicks;

  EcsEntityId mainWindow;
  SndObjectId musicHandle;

  EcsEntityId  creditsAsset;
  UiScrollview creditsScrollView;
  f32          creditsHeight;

  u32         levelMask;
  u32         levelLoadingMask;
  EcsEntityId levelAssets[GameLevelsMax];
  String      levelNames[GameLevelsMax];

  f32 prevGrayscaleFrac;
  f32 prevBloomIntensity;
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

static String game_state_name(const GameState state) {
  static const String g_names[] = {
      [GameState_None]        = string_static("None"),
      [GameState_MenuMain]    = string_static("MenuMain"),
      [GameState_MenuSelect]  = string_static("MenuSelect"),
      [GameState_MenuCredits] = string_static("MenuCredits"),
      [GameState_Loading]     = string_static("Loading"),
      [GameState_Play]        = string_static("Play"),
      [GameState_Edit]        = string_static("Edit"),
      [GameState_Pause]       = string_static("Pause"),
      [GameState_Result]      = string_static("Result"),
  };
  ASSERT(array_elems(g_names) == GameState_Count, "Incorrect number of names");
  return g_names[state];
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

  const EcsEntityId   uiCanvas = ui_canvas_create(world, window, UiCanvasCreateFlags_ToBack);
  GameMainWindowComp* gameWinComp =
      ecs_world_add_t(world, window, GameMainWindowComp, .uiCanvas = uiCanvas);

  if (devSupport) {
    dev_log_viewer_create(world, window, LogMask_Info | LogMask_Warn | LogMask_Error);
    gameWinComp->devMenu = dev_menu_create(world, window, true /* hidden */);
  }

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

static void game_music_stop(GameComp* game, SndMixerComp* soundMixer) {
  if (!sentinel_check(game->musicHandle)) {
    snd_object_stop(soundMixer, game->musicHandle);
    game->musicHandle = sentinel_u32;
  }
}

static void game_music_play(
    EcsWorld* world, GameComp* game, SndMixerComp* soundMixer, AssetManagerComp* assets) {

  if (!sentinel_check(game->musicHandle)) {
    return; // Already playing.
  }
  const String assetPattern = string_lit("external/music/*.wav");
  EcsEntityId  assetEntities[asset_query_max_results];
  const u32    assetCount = asset_query(world, assets, assetPattern, assetEntities);

  if (assetCount && snd_object_new(soundMixer, &game->musicHandle) == SndResult_Success) {
    const u32 assetIndex = (u32)rng_sample_range(g_rng, 0, assetCount);
    snd_object_set_asset(soundMixer, game->musicHandle, assetEntities[assetIndex]);
    snd_object_set_looping(soundMixer, game->musicHandle);
  }
}

static void game_sound_play(
    EcsWorld* world, SndMixerComp* soundMixer, AssetManagerComp* assets, const String id) {

  SndObjectId sndHandle;
  if (snd_object_new(soundMixer, &sndHandle) == SndResult_Success) {
    snd_object_set_asset(soundMixer, sndHandle, asset_lookup(world, assets, id));
  }
}

static f32 game_exposure_value_get(const GamePrefsComp* prefs) {
  static const f32 g_exposureMin = 0.25f;
  static const f32 g_exposureMax = 1.75f;
  return math_lerp(g_exposureMin, g_exposureMax, prefs->exposure);
}

static u16 game_limiter_freq_get(const GamePrefsComp* prefs) {
  switch (prefs->limiter) {
  case GameLimiter_Off:
    return 0;
  case GameLimiter_30:
    return 30;
  case GameLimiter_60:
    return 60;
  case GameLimiter_90:
    return 90;
  case GameLimiter_120:
    return 120;
  case GameLimiter_Count:
    break;
  }
  diag_crash();
}

static void game_quality_apply(
    const GamePrefsComp*    prefs,
    RendSettingsGlobalComp* rendSetGlobal,
    RendSettingsComp*       rendSetWin) {

  rendSetGlobal->limiterFreq = game_limiter_freq_get(prefs);
  rendSetWin->presentMode = prefs->vsync ? RendPresentMode_VSyncRelaxed : RendPresentMode_Mailbox;

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

static void game_ui_settings_apply(const GamePrefsComp* prefs, UiSettingsGlobalComp* uiSettings) {
  switch (prefs->uiScale) {
  case GameUiScale_Small:
    uiSettings->scale = 0.75f;
    break;
  case GameUiScale_Normal:
    uiSettings->scale = 1.0f;
    break;
  case GameUiScale_Big:
    uiSettings->scale = 1.25f;
    break;
  case GameUiScale_VeryBig:
    uiSettings->scale = 1.5f;
    break;
  case GameUiScale_Count:
    UNREACHABLE
  }
}

typedef struct {
  EcsWorld*                    world;
  GameComp*                    game;
  GamePrefsComp*               prefs;
  SceneLevelManagerComp*       levelManager;
  const SceneTerrainComp*      terrain;
  InputManagerComp*            input;
  SndMixerComp*                soundMixer;
  LocManagerComp*              locManager;
  const SceneMissionComp*      mission;
  const SceneFactionStatsComp* factionStats;
  const SceneTimeComp*         time;
  SceneTimeSettingsComp*       timeSet;
  GameCmdComp*                 cmd;
  AssetManagerComp*            assets;
  SceneVisibilityEnvComp*      visibilityEnv;
  RendSettingsGlobalComp*      rendSetGlobal;
  UiSettingsGlobalComp*        uiSetGlobal;
  DevStatsGlobalComp*          devStatsGlobal;

  EcsEntityId         winEntity;
  GameMainWindowComp* winGame;
  GapWindowComp*      winComp;
  RendSettingsComp*   winRendSet;
  GameHudComp*        winHud;
  GameInputComp*      winGameInput;
  DevStatsComp*       winDevStats;
  DevMenuComp*        winDevMenu;
  UiCanvasComp*       winCanvas;

  EcsView* levelRenderableView;
  EcsView* rawAssetView;
  EcsView* devPanelView;      // Null if dev-support is not enabled.
  EcsView* devLevelPanelView; // Null if dev-support is not enabled.
} GameUpdateContext;

static void game_notify_level_action(const GameUpdateContext* ctx, const String action) {
  if (ctx->devStatsGlobal) {
    String name = scene_level_name(ctx->levelManager);
    if (string_is_empty(name)) {
      name = string_lit("<unnamed>");
    }
    dev_stats_notify(ctx->devStatsGlobal, action, name);
  }
}

static void game_toggle_camera(const GameUpdateContext* ctx) {
  if (!ctx->winGameInput) {
    return;
  }
  if (game_input_type(ctx->winGameInput) == GameInputType_Normal) {
    game_input_type_set(ctx->winGameInput, GameInputType_FreeCamera);
    dev_stats_notify(ctx->devStatsGlobal, string_lit("Camera"), string_lit("Free"));
  } else {
    game_input_type_set(ctx->winGameInput, GameInputType_Normal);
    dev_stats_notify(ctx->devStatsGlobal, string_lit("Camera"), string_lit("Normal"));
  }
}

static void game_fullscreen_toggle(const GameUpdateContext* ctx) {
  if (gap_window_mode(ctx->winComp) == GapWindowMode_Fullscreen) {
    log_i("Enter windowed mode");
    const GapVector size = gap_window_param(ctx->winComp, GapParam_WindowSizePreFullscreen);
    gap_window_resize(ctx->winComp, size, GapWindowMode_Windowed);
    gap_window_flags_unset(ctx->winComp, GapWindowFlags_CursorConfine);
  } else {
    log_i("Enter fullscreen mode");
    gap_window_resize(ctx->winComp, gap_vector(0, 0), GapWindowMode_Fullscreen);
    gap_window_flags_set(ctx->winComp, GapWindowFlags_CursorConfine);
  }
}

static void game_quit(const GameUpdateContext* ctx) {
  log_i("Quit");
  gap_window_close(ctx->winComp);
}

static void game_transition_delayed(GameComp* game, const GameState state) {
  game->stateNext = state;
}

static void game_transition(const GameUpdateContext* ctx, const GameState state) {
  if (ctx->game->state == state) {
    return;
  }
  ctx->game->statePrev  = ctx->game->state;
  ctx->game->state      = state;
  ctx->game->stateTicks = 0;

  if (ctx->devStatsGlobal) {
    dev_stats_notify(ctx->devStatsGlobal, string_lit("GameState"), game_state_name(state));
  }

  // Apply leave transitions.
  switch (ctx->game->statePrev) {
  case GameState_Loading:
    game_music_stop(ctx->game, ctx->soundMixer);
    ctx->timeSet->flags &= ~SceneTimeFlags_Paused;
    break;
  case GameState_Play:
    input_layer_disable(ctx->input, GameId_Game);
    game_input_type_set(ctx->winGameInput, GameInputType_None);
    asset_loading_budget_set(ctx->assets, 0); // Infinite budget while not in gameplay.
    break;
  case GameState_Edit:
    input_layer_disable(ctx->input, GameId_Edit);
    game_input_type_set(ctx->winGameInput, GameInputType_None);
    dev_stats_debug_set(ctx->winDevStats, DevStatDebug_Off);
    if (ctx->winDevMenu) {
      dev_menu_edit_panels_close(ctx->world, ctx->winDevMenu);
    }
    ctx->game->flags &= ~GameFlags_EditMode;
    break;
  case GameState_Pause:
  case GameState_Result:
    ctx->timeSet->flags &= ~SceneTimeFlags_Paused;

    ctx->winRendSet->bloomIntensity = ctx->game->prevBloomIntensity;
    ctx->winRendSet->grayscaleFrac  = ctx->game->prevGrayscaleFrac;
    ctx->winRendSet->exposure       = game_exposure_value_get(ctx->prefs);
    break;
  default:
    break;
  }

  // Apply enter transitions.
  switch (ctx->game->state) {
  case GameState_MenuMain:
    game_music_play(ctx->world, ctx->game, ctx->soundMixer, ctx->assets);
    scene_level_unload(ctx->world);
    ctx->winRendSet->flags |= RendFlags_2D;
    if (ctx->winDevStats) {
      dev_stats_debug_set_available(ctx->winDevStats);
    }
    break;
  case GameState_Loading:
    ctx->timeSet->flags |= SceneTimeFlags_Paused;
    ctx->winRendSet->flags |= RendFlags_2D;
    break;
  case GameState_Play:
    ctx->winRendSet->flags &= ~RendFlags_2D;
    game_input_type_set(ctx->winGameInput, GameInputType_Normal);
    asset_loading_budget_set(ctx->assets, time_milliseconds(2)); // Limit loading during gameplay.
    break;
  case GameState_Edit:
    ctx->winRendSet->flags &= ~RendFlags_2D;
    game_input_type_set(ctx->winGameInput, GameInputType_Normal);
    input_layer_enable(ctx->input, GameId_Edit);
    if (ctx->winDevMenu) {
      dev_menu_edit_panels_open(ctx->world, ctx->winDevMenu);
    }
    if (ctx->winDevStats) {
      dev_stats_debug_set(ctx->winDevStats, DevStatDebug_Unavailable);
    }
    break;
  case GameState_Pause:
  case GameState_Result:
    ctx->timeSet->flags |= SceneTimeFlags_Paused;

    ctx->winRendSet->exposure = 0.05f;

    ctx->game->prevGrayscaleFrac   = ctx->winRendSet->grayscaleFrac;
    ctx->winRendSet->grayscaleFrac = 0.75f;

    ctx->game->prevBloomIntensity   = ctx->winRendSet->bloomIntensity;
    ctx->winRendSet->bloomIntensity = 1.0f;
    break;
  default:
    break;
  }
}

static void menu_draw_version(const GameUpdateContext* ctx) {
  const UiVector size = ui_vector(500, 25);

  ui_layout_push(ctx->winCanvas);
  ui_layout_inner(ctx->winCanvas, UiBase_Canvas, UiAlign_BottomLeft, size, UiBase_Absolute);
  ui_layout_move(ctx->winCanvas, ui_vector(4, 2), UiBase_Absolute, Ui_XY);

  ui_style_push(ctx->winCanvas);
  ui_style_color(ctx->winCanvas, ui_color(255, 255, 255, 128));
  ui_style_outline(ctx->winCanvas, 1);
  ui_label(
      ctx->winCanvas,
      fmt_write_scratch("v{}", fmt_text(version_str_scratch(g_versionExecutable))),
      .align    = UiAlign_BottomLeft,
      .fontSize = 12);
  ui_style_pop(ctx->winCanvas);
  ui_layout_pop(ctx->winCanvas);
}

static void menu_draw_spinner(const GameUpdateContext* ctx) {
  const u32 segments  = 8;
  const f32 radius    = 25.0f;
  const f32 rotSpeed  = -3.5f;
  const f32 seconds   = scene_real_time_seconds(ctx->time);
  const f32 baseAngle = math_mod_f32(seconds * rotSpeed, math_pi_f32 * 2.0f);
  const f32 angleStep = math_pi_f32 * 2.0f / segments;

  ui_layout_push(ctx->winCanvas);
  ui_layout_move_to(ctx->winCanvas, UiBase_Canvas, UiAlign_MiddleCenter, Ui_XY);
  ui_layout_resize(ctx->winCanvas, UiAlign_MiddleCenter, ui_vector(10, 10), UiBase_Absolute, Ui_XY);
  for (u32 i = 0; i != segments; ++i) {
    const f32      angle = baseAngle + i * angleStep;
    const UiVector pos   = ui_vector(radius * math_cos_f32(angle), radius * math_sin_f32(angle));

    ui_layout_push(ctx->winCanvas);
    ui_layout_move(ctx->winCanvas, pos, UiBase_Absolute, Ui_XY);
    ui_canvas_draw_glyph(ctx->winCanvas, UiShape_Circle, 0, UiFlags_None);
    ui_layout_pop(ctx->winCanvas);
  }
  ui_layout_pop(ctx->winCanvas);
}

static void menu_draw_entry_frame(const GameUpdateContext* ctx) {
  ui_style_push(ctx->winCanvas);
  ui_style_outline(ctx->winCanvas, 5);
  ui_style_color(ctx->winCanvas, ui_color_clear);
  ui_canvas_draw_glyph(ctx->winCanvas, UiShape_Circle, 10, UiFlags_None);
  ui_style_pop(ctx->winCanvas);
}

typedef void (*MenuEntryFunc)(const GameUpdateContext*, u32 index);

typedef struct {
  MenuEntryFunc func;
  u32           size; // In multiples of the default size.
} MenuEntry;

static void menu_draw(
    const GameUpdateContext* ctx,
    const String             header,
    const f32                width,
    const MenuEntry          entries[],
    const u32                count) {
  static const UiVector g_headerSize  = {.x = 500.0f, .y = 100.0f};
  static const f32      g_entryHeight = 50.0f;
  static const f32      g_spacing     = 8.0f;

  ui_style_push(ctx->winCanvas);
  ui_style_transform(ctx->winCanvas, UiTransform_ToUpper);

  f32 totalHeight = 0.0f;
  if (!string_is_empty(header)) {
    totalHeight += g_headerSize.y;
  }
  for (u32 i = 0; i != count; ++i) {
    totalHeight += i != 0 ? g_spacing : 0.0f;
    totalHeight += g_entryHeight * entries[i].size;
  }

  ui_layout_move_to(ctx->winCanvas, UiBase_Container, UiAlign_MiddleCenter, Ui_XY);
  ui_layout_move(ctx->winCanvas, ui_vector(0, totalHeight * 0.5f), UiBase_Absolute, Ui_Y);

  if (!string_is_empty(header)) {
    ui_layout_push(ctx->winCanvas);
    ui_layout_resize(ctx->winCanvas, UiAlign_TopCenter, g_headerSize, UiBase_Absolute, Ui_XY);

    ui_style_push(ctx->winCanvas);
    ui_style_outline(ctx->winCanvas, 5);
    ui_style_weight(ctx->winCanvas, UiWeight_Heavy);
    ui_style_color(ctx->winCanvas, ui_color(255, 173, 10, 255));
    ui_label(ctx->winCanvas, header, .align = UiAlign_MiddleCenter, .fontSize = 60);
    ui_style_pop(ctx->winCanvas);

    ui_layout_pop(ctx->winCanvas);
    ui_layout_move_dir(ctx->winCanvas, Ui_Down, g_headerSize.y, UiBase_Absolute);
  }

  for (u32 i = 0; i != count; ++i) {
    const UiVector size = {width, g_entryHeight * entries[i].size};
    ui_layout_push(ctx->winCanvas);
    ui_layout_resize(ctx->winCanvas, UiAlign_TopCenter, size, UiBase_Absolute, Ui_XY);
    entries[i].func(ctx, i);
    ui_layout_pop(ctx->winCanvas);
    ui_layout_move_dir(ctx->winCanvas, Ui_Down, size.y + g_spacing, UiBase_Absolute);
  }
  ui_style_pop(ctx->winCanvas);
}

static void
menu_bar_draw(const GameUpdateContext* ctx, const MenuEntry entries[], const u32 count) {
  static const UiVector g_entrySize = {.x = 40.0f, .y = 40.0f};
  static const f32      g_spacing   = 8.0f;

  const f32 xCenterOffset = (count - 1) * (g_entrySize.x + g_spacing) * -0.5f;
  ui_layout_inner(
      ctx->winCanvas, UiBase_Canvas, UiAlign_BottomCenter, g_entrySize, UiBase_Absolute);
  ui_layout_move(ctx->winCanvas, ui_vector(xCenterOffset, g_spacing), UiBase_Absolute, Ui_XY);

  for (u32 i = 0; i != count; ++i) {
    entries[i].func(ctx, i);
    ui_layout_next(ctx->winCanvas, Ui_Right, g_spacing);
  }
}

static void menu_entry_play(const GameUpdateContext* ctx, MAYBE_UNUSED const u32 index) {
  if (ui_button(
          ctx->winCanvas,
          .label    = loc_translate(GameId_MENU_PLAY),
          .fontSize = 25,
          .tooltip  = loc_translate(GameId_MENU_PLAY_TOOLTIP))) {
    ctx->game->flags &= ~GameFlags_EditMode;
    game_transition(ctx, GameState_MenuSelect);
  }
}

static void menu_entry_edit(const GameUpdateContext* ctx, MAYBE_UNUSED const u32 index) {
  if (ui_button(
          ctx->winCanvas,
          .label      = loc_translate(GameId_MENU_EDIT),
          .frameColor = ui_color(255, 16, 16, 192),
          .fontSize   = 25,
          .tooltip    = loc_translate(GameId_MENU_EDIT_TOOLTIP))) {
    ctx->game->flags |= GameFlags_EditMode;
    game_transition(ctx, GameState_MenuSelect);
  }
}

static void menu_entry_credits(const GameUpdateContext* ctx, MAYBE_UNUSED const u32 index) {
  if (ui_button(
          ctx->winCanvas,
          .label    = loc_translate(GameId_MENU_CREDITS),
          .fontSize = 25,
          .tooltip  = loc_translate(GameId_MENU_CREDITS_TOOLTIP))) {
    game_transition(ctx, GameState_MenuCredits);
  }
}

static void menu_entry_resume(const GameUpdateContext* ctx, MAYBE_UNUSED const u32 index) {
  if (ui_button(
          ctx->winCanvas,
          .label    = loc_translate(GameId_MENU_RESUME),
          .fontSize = 25,
          .tooltip  = loc_translate(GameId_MENU_RESUME_TOOLTIP),
          .activate = input_triggered(ctx->input, GameId_Pause), )) {
    game_transition_delayed(ctx->game, GameState_Play);
  }
}

static void menu_entry_restart(const GameUpdateContext* ctx, MAYBE_UNUSED const u32 index) {
  if (ui_button(
          ctx->winCanvas,
          .label    = loc_translate(GameId_MENU_RESTART),
          .fontSize = 25,
          .tooltip  = loc_translate(GameId_MENU_RESTART_TOOLTIP))) {
    game_transition(ctx, GameState_Loading);
    scene_level_reload(ctx->world, SceneLevelMode_Play);
  }
}

static void menu_entry_edit_current(const GameUpdateContext* ctx, MAYBE_UNUSED const u32 index) {
  if (ui_button(
          ctx->winCanvas,
          .label      = loc_translate(GameId_MENU_EDIT_CURRENT),
          .frameColor = ui_color(255, 16, 16, 192),
          .fontSize   = 25,
          .tooltip    = loc_translate(GameId_MENU_EDIT_CURRENT_TOOLTIP))) {
    ctx->game->flags |= GameFlags_EditMode;
    scene_level_reload(ctx->world, SceneLevelMode_Edit);
    game_transition(ctx, GameState_Loading);
  }
}

static void menu_entry_menu_main(const GameUpdateContext* ctx, MAYBE_UNUSED const u32 index) {
  if (ui_button(
          ctx->winCanvas,
          .label    = loc_translate(GameId_MENU_MAINMENU),
          .fontSize = 25,
          .tooltip  = loc_translate(GameId_MENU_MAINMENU_TOOLTIP))) {
    game_transition(ctx, GameState_MenuMain);
  }
}

static void menu_entry_volume(const GameUpdateContext* ctx, MAYBE_UNUSED const u32 index) {
  menu_draw_entry_frame(ctx);

  ui_layout_push(ctx->winCanvas);
  static const UiVector g_frameInset = {-40, -10};
  ui_layout_grow(ctx->winCanvas, UiAlign_MiddleCenter, g_frameInset, UiBase_Absolute, Ui_XY);
  ui_label(ctx->winCanvas, loc_translate(GameId_MENU_VOLUME));
  ui_layout_inner(
      ctx->winCanvas, UiBase_Current, UiAlign_MiddleRight, ui_vector(0.5f, 1), UiBase_Current);
  if (ui_slider(
          ctx->winCanvas,
          &ctx->prefs->volume,
          .max        = 1e2f,
          .step       = 1,
          .handleSize = 25,
          .thickness  = 10,
          .tooltip    = loc_translate(GameId_MENU_VOLUME_TOOLTIP))) {
    ctx->prefs->dirty = true;
    snd_mixer_gain_set(ctx->soundMixer, ctx->prefs->volume * 1e-2f);
  }
  ui_layout_pop(ctx->winCanvas);
}

static void menu_entry_exposure(const GameUpdateContext* ctx, MAYBE_UNUSED const u32 index) {
  menu_draw_entry_frame(ctx);

  ui_layout_push(ctx->winCanvas);
  static const UiVector g_frameInset = {-40, -10};
  ui_layout_grow(ctx->winCanvas, UiAlign_MiddleCenter, g_frameInset, UiBase_Absolute, Ui_XY);
  ui_label(ctx->winCanvas, loc_translate(GameId_MENU_EXPOSURE));
  ui_layout_inner(
      ctx->winCanvas, UiBase_Current, UiAlign_MiddleRight, ui_vector(0.5f, 1), UiBase_Current);
  if (ui_slider(
          ctx->winCanvas,
          &ctx->prefs->exposure,
          .handleSize = 25,
          .thickness  = 10,
          .tooltip    = loc_translate(GameId_MENU_EXPOSURE_TOOLTIP))) {
    ctx->prefs->dirty = true;
    if (ctx->game->state != GameState_Pause) {
      ctx->winRendSet->exposure = game_exposure_value_get(ctx->prefs);
    }
  }
  ui_layout_pop(ctx->winCanvas);
}

static void menu_entry_vsync(const GameUpdateContext* ctx, MAYBE_UNUSED const u32 index) {
  menu_draw_entry_frame(ctx);

  ui_layout_push(ctx->winCanvas);
  static const UiVector g_frameInset = {-40, -10};
  ui_layout_grow(ctx->winCanvas, UiAlign_MiddleCenter, g_frameInset, UiBase_Absolute, Ui_XY);
  ui_label(ctx->winCanvas, loc_translate(GameId_MENU_VSYNC));
  if (ui_toggle(
          ctx->winCanvas,
          &ctx->prefs->vsync,
          .align   = UiAlign_MiddleRight,
          .size    = 25,
          .tooltip = loc_translate(GameId_MENU_VSYNC_TOOLTIP))) {
    ctx->prefs->dirty = true;
    game_quality_apply(ctx->prefs, ctx->rendSetGlobal, ctx->winRendSet);
  }
  ui_layout_pop(ctx->winCanvas);
}

static void menu_entry_limiter(const GameUpdateContext* ctx, MAYBE_UNUSED const u32 index) {
  menu_draw_entry_frame(ctx);

  ui_layout_push(ctx->winCanvas);
  static const UiVector g_frameInset = {-40, -10};
  ui_layout_grow(ctx->winCanvas, UiAlign_MiddleCenter, g_frameInset, UiBase_Absolute, Ui_XY);
  ui_label(ctx->winCanvas, loc_translate(GameId_MENU_LIMITER));
  ui_layout_inner(
      ctx->winCanvas, UiBase_Current, UiAlign_MiddleRight, ui_vector(0.5f, 0.6f), UiBase_Current);

  ui_style_push(ctx->winCanvas);
  ui_style_transform(ctx->winCanvas, UiTransform_None);

  i32* limiter = (i32*)&ctx->prefs->limiter;
  if (ui_select(
          ctx->winCanvas,
          limiter,
          g_gameLimiterLabels,
          GameLimiter_Count,
          .tooltip = loc_translate(GameId_MENU_LIMITER_TOOLTIP),
          .flags   = UiWidget_Translate)) {
    ctx->prefs->dirty = true;
    game_quality_apply(ctx->prefs, ctx->rendSetGlobal, ctx->winRendSet);
  }

  ui_style_pop(ctx->winCanvas);
  ui_layout_pop(ctx->winCanvas);
}

static void menu_entry_quality(const GameUpdateContext* ctx, MAYBE_UNUSED const u32 index) {
  menu_draw_entry_frame(ctx);

  ui_layout_push(ctx->winCanvas);
  static const UiVector g_frameInset = {-40, -10};
  ui_layout_grow(ctx->winCanvas, UiAlign_MiddleCenter, g_frameInset, UiBase_Absolute, Ui_XY);
  ui_label(ctx->winCanvas, loc_translate(GameId_MENU_QUALITY));
  ui_layout_inner(
      ctx->winCanvas, UiBase_Current, UiAlign_MiddleRight, ui_vector(0.5f, 0.6f), UiBase_Current);

  ui_style_push(ctx->winCanvas);
  ui_style_transform(ctx->winCanvas, UiTransform_None);

  i32* quality = (i32*)&ctx->prefs->quality;
  if (ui_select(
          ctx->winCanvas,
          quality,
          g_gameQualityLabels,
          GameQuality_Count,
          .tooltip = loc_translate(GameId_MENU_QUALITY_TOOLTIP),
          .flags   = UiWidget_Translate)) {
    ctx->prefs->dirty = true;
    game_quality_apply(ctx->prefs, ctx->rendSetGlobal, ctx->winRendSet);
  }

  ui_style_pop(ctx->winCanvas);
  ui_layout_pop(ctx->winCanvas);
}

static void menu_entry_ui_scale(const GameUpdateContext* ctx, MAYBE_UNUSED const u32 index) {
  menu_draw_entry_frame(ctx);

  ui_layout_push(ctx->winCanvas);
  static const UiVector g_frameInset = {-40, -10};
  ui_layout_grow(ctx->winCanvas, UiAlign_MiddleCenter, g_frameInset, UiBase_Absolute, Ui_XY);
  ui_label(ctx->winCanvas, loc_translate(GameId_MENU_UI_SCALE));
  ui_layout_inner(
      ctx->winCanvas, UiBase_Current, UiAlign_MiddleRight, ui_vector(0.5f, 0.6f), UiBase_Current);

  ui_style_push(ctx->winCanvas);
  ui_style_transform(ctx->winCanvas, UiTransform_None);

  i32* uiScale = (i32*)&ctx->prefs->uiScale;
  if (ui_select(
          ctx->winCanvas,
          uiScale,
          g_gameUiScaleLabels,
          GameUiScale_Count,
          .tooltip = loc_translate(GameId_MENU_UI_SCALE_TOOLTIP),
          .flags   = UiWidget_Translate)) {
    ctx->prefs->dirty = true;
    game_ui_settings_apply(ctx->prefs, ctx->uiSetGlobal);
  }

  ui_style_pop(ctx->winCanvas);
  ui_layout_pop(ctx->winCanvas);
}

static void menu_entry_locale(const GameUpdateContext* ctx, MAYBE_UNUSED const u32 index) {
  menu_draw_entry_frame(ctx);

  ui_layout_push(ctx->winCanvas);
  static const UiVector g_frameInset = {-40, -10};
  ui_layout_grow(ctx->winCanvas, UiAlign_MiddleCenter, g_frameInset, UiBase_Absolute, Ui_XY);
  ui_label(ctx->winCanvas, loc_translate(GameId_MENU_LOCALE));
  ui_layout_inner(
      ctx->winCanvas, UiBase_Current, UiAlign_MiddleRight, ui_vector(0.5f, 0.6f), UiBase_Current);

  ui_style_push(ctx->winCanvas);
  ui_style_transform(ctx->winCanvas, UiTransform_None);

  i32 localeIndex = (i32)loc_manager_active_get(ctx->locManager);
  if (ui_select(
          ctx->winCanvas,
          &localeIndex,
          loc_manager_locale_names(ctx->locManager),
          loc_manager_locale_count(ctx->locManager),
          .tooltip = loc_translate(GameId_MENU_LOCALE_TOOLTIP))) {
    loc_manager_active_set(ctx->locManager, (u32)localeIndex);
    game_prefs_locale_set(ctx->prefs, loc_manager_active_id(ctx->locManager));
  }

  ui_style_pop(ctx->winCanvas);
  ui_layout_pop(ctx->winCanvas);
}

static void menu_entry_fullscreen(const GameUpdateContext* ctx, MAYBE_UNUSED const u32 index) {
  menu_draw_entry_frame(ctx);

  ui_layout_push(ctx->winCanvas);
  static const UiVector g_frameInset = {-40, -10};
  ui_layout_grow(ctx->winCanvas, UiAlign_MiddleCenter, g_frameInset, UiBase_Absolute, Ui_XY);
  ui_label(ctx->winCanvas, loc_translate(GameId_MENU_FULLSCREEN));
  bool isFullscreen = gap_window_mode(ctx->winComp) == GapWindowMode_Fullscreen;
  if (ui_toggle(
          ctx->winCanvas,
          &isFullscreen,
          .align   = UiAlign_MiddleRight,
          .size    = 25,
          .tooltip = loc_translate(GameId_MENU_FULLSCREEN_TOOLTIP))) {
    game_fullscreen_toggle(ctx);
  }
  ui_layout_pop(ctx->winCanvas);
}

static void menu_entry_quit(const GameUpdateContext* ctx, MAYBE_UNUSED const u32 index) {
  if (ui_button(
          ctx->winCanvas,
          .label    = loc_translate(GameId_MENU_QUIT),
          .fontSize = 25,
          .tooltip  = loc_translate(GameId_MENU_QUIT_TOOLTIP))) {
    game_quit(ctx);
  }
}

static void menu_entry_back(const GameUpdateContext* ctx, MAYBE_UNUSED const u32 index) {
  ui_layout_push(ctx->winCanvas);
  ui_style_outline(ctx->winCanvas, 4);
  if (ui_button(
          ctx->winCanvas,
          .label      = ui_shape_scratch(UiShape_ArrowLeft),
          .fontSize   = 35,
          .frameColor = ui_color_clear,
          .activate   = input_triggered(ctx->input, GameId_Back),
          .tooltip    = loc_translate(GameId_MENU_BACK_TOOLTIP))) {
    game_transition(ctx, ctx->game->statePrev);
  }
  ui_layout_pop(ctx->winCanvas);
}

static void menu_entry_stat(const GameUpdateContext* ctx, const StringHash key, const String val) {
  menu_draw_entry_frame(ctx);

  ui_layout_push(ctx->winCanvas);
  static const UiVector g_frameInset = {-40, -10};
  ui_layout_grow(ctx->winCanvas, UiAlign_MiddleCenter, g_frameInset, UiBase_Absolute, Ui_XY);
  ui_label(ctx->winCanvas, loc_translate(key));
  ui_layout_inner(
      ctx->winCanvas, UiBase_Current, UiAlign_MiddleRight, ui_vector(0.3f, 1.0f), UiBase_Current);

  ui_style_push(ctx->winCanvas);
  ui_style_transform(ctx->winCanvas, UiTransform_None);
  ui_label(ctx->winCanvas, val);
  ui_style_pop(ctx->winCanvas);

  ui_layout_pop(ctx->winCanvas);
}

static void menu_entry_stat_time(const GameUpdateContext* ctx, MAYBE_UNUSED const u32 index) {
  const TimeDuration time = ctx->time->levelTime;
  menu_entry_stat(ctx, GameId_MENU_STAT_TIME, fmt_write_scratch("{}", fmt_duration(time)));
}

static void menu_entry_stat_completed(const GameUpdateContext* ctx, MAYBE_UNUSED const u32 index) {
  const usize count = scene_mission_obj_count_in_state(ctx->mission, SceneMissionState_Success);
  menu_entry_stat(ctx, GameId_MENU_STAT_COMPLETED, fmt_write_scratch("{}", fmt_int(count)));
}

static void menu_entry_stat_failed(const GameUpdateContext* ctx, MAYBE_UNUSED const u32 index) {
  const usize count = scene_mission_obj_count_in_state(ctx->mission, SceneMissionState_Fail);
  menu_entry_stat(ctx, GameId_MENU_STAT_FAILED, fmt_write_scratch("{}", fmt_int(count)));
}

static void menu_entry_stat_kills(const GameUpdateContext* ctx, MAYBE_UNUSED const u32 index) {
  u32 kills = 0;
  if (ctx->factionStats) {
    kills = (u32)ctx->factionStats->values[SceneFaction_A][SceneFactionStat_Kills];
  }
  menu_entry_stat(ctx, GameId_MENU_STAT_KILLS, fmt_write_scratch("{}", fmt_int(kills)));
}

static void menu_entry_stat_losses(const GameUpdateContext* ctx, MAYBE_UNUSED const u32 index) {
  u32 losses = 0;
  if (ctx->factionStats) {
    losses = (u32)ctx->factionStats->values[SceneFaction_A][SceneFactionStat_Losses];
  }
  menu_entry_stat(ctx, GameId_MENU_STAT_LOSSES, fmt_write_scratch("{}", fmt_int(losses)));
}

static void menu_entry_refresh_levels(const GameUpdateContext* ctx, MAYBE_UNUSED const u32 index) {
  ui_layout_push(ctx->winCanvas);
  ui_style_outline(ctx->winCanvas, 4);
  if (ui_button(
          ctx->winCanvas,
          .label      = ui_shape_scratch(UiShape_Restart),
          .fontSize   = 35,
          .frameColor = ui_color_clear,
          .flags      = ctx->game->levelLoadingMask ? UiWidget_Disabled : UiWidget_Default,
          .tooltip    = loc_translate(GameId_MENU_LEVEL_REFRESH_TOOLTIP))) {
    ctx->game->flags |= GameFlags_RefreshLevels;
  }
  ui_layout_pop(ctx->winCanvas);
}

static void menu_entry_level(const GameUpdateContext* ctx, u32 index) {
  const u32    levelIndex = (u32)bitset_nth(bitset_from_var(ctx->game->levelMask), index);
  const String levelName  = loc_translate_str(ctx->game->levelNames[levelIndex]);

  String tooltip;
  if (ctx->game->flags & GameFlags_EditMode) {
    tooltip = loc_translate_fmt(GameId_MENU_LEVEL_EDIT_TOOLTIP, fmt_text(levelName));
  } else {
    tooltip = loc_translate_fmt(GameId_MENU_LEVEL_PLAY_TOOLTIP, fmt_text(levelName));
  }

  if (ui_button(ctx->winCanvas, .label = levelName, .fontSize = 25, .tooltip = tooltip)) {
    game_transition(ctx, GameState_Loading);
    SceneLevelMode levelMode;
    if (ctx->game->flags & GameFlags_EditMode) {
      levelMode = SceneLevelMode_Edit;
    } else {
      levelMode = SceneLevelMode_Play;
    }
    scene_level_load(ctx->world, levelMode, ctx->game->levelAssets[levelIndex]);
  }
}

static void menu_entry_credits_content(const GameUpdateContext* ctx, MAYBE_UNUSED const u32 index) {
  menu_draw_entry_frame(ctx);

  EcsIterator* creditsItr = ecs_view_maybe_at(ctx->rawAssetView, ctx->game->creditsAsset);

  ui_layout_push(ctx->winCanvas);
  ui_layout_grow(ctx->winCanvas, UiAlign_MiddleCenter, ui_vector(-25, -25), UiBase_Absolute, Ui_XY);

  ui_style_push(ctx->winCanvas);
  ui_scrollview_begin(
      ctx->winCanvas, &ctx->game->creditsScrollView, UiLayer_Normal, ctx->game->creditsHeight);

  ui_style_transform(ctx->winCanvas, UiTransform_None);
  ui_style_weight(ctx->winCanvas, UiWeight_Light);

  const String  text = creditsItr ? ecs_view_read_t(creditsItr, AssetRawComp)->data : string_empty;
  const UiId    textId     = ui_canvas_id_peek(ctx->winCanvas);
  const UiFlags textFlags  = UiFlags_VerticalOverflow | UiFlags_TightTextRect | UiFlags_TrackRect;
  ctx->game->creditsHeight = ui_canvas_elem_rect(ctx->winCanvas, textId).height;
  ui_canvas_draw_text(ctx->winCanvas, text, 16, UiAlign_TopLeft, textFlags);

  ui_scrollview_end(ctx->winCanvas, &ctx->game->creditsScrollView);
  ui_style_pop(ctx->winCanvas);
  ui_layout_pop(ctx->winCanvas);
}

static void menu_entry_edit_camera(const GameUpdateContext* ctx, MAYBE_UNUSED const u32 index) {
  if (ui_button(
          ctx->winCanvas,
          .label    = ui_shape_scratch(UiShape_PhotoCamera),
          .fontSize = 25,
          .tooltip  = loc_translate(GameId_MENU_EDIT_CAMERA_TOOLTIP))) {
    game_toggle_camera(ctx);
  }
}

static void menu_entry_edit_play(const GameUpdateContext* ctx, MAYBE_UNUSED const u32 index) {
  if (ui_button(
          ctx->winCanvas,
          .label    = ui_shape_scratch(UiShape_Play),
          .fontSize = 25,
          .tooltip  = loc_translate(GameId_MENU_EDIT_PLAY_TOOLTIP))) {
    scene_level_save_reload(ctx->world, scene_level_asset(ctx->levelManager), SceneLevelMode_Play);
    game_transition_delayed(ctx->game, GameState_Loading);
  }
}

static void menu_entry_edit_discard(const GameUpdateContext* ctx, MAYBE_UNUSED const u32 index) {
  if (ui_button(
          ctx->winCanvas,
          .label    = ui_shape_scratch(UiShape_Restart),
          .fontSize = 25,
          .tooltip  = loc_translate(GameId_MENU_EDIT_DISCARD_TOOLTIP))) {
    scene_level_reload(ctx->world, SceneLevelMode_Edit);
    game_notify_level_action(ctx, string_lit("Discard"));
  }
}

static void menu_entry_edit_save(const GameUpdateContext* ctx, MAYBE_UNUSED const u32 index) {
  if (ui_button(
          ctx->winCanvas,
          .label    = ui_shape_scratch(UiShape_Save),
          .fontSize = 25,
          .activate = input_triggered(ctx->input, GameId_SaveLevel),
          .tooltip  = loc_translate(GameId_MENU_EDIT_SAVE_TOOLTIP))) {
    scene_level_save(ctx->world, scene_level_asset(ctx->levelManager));
    game_notify_level_action(ctx, string_lit("Save"));
  }
}

static void menu_entry_edit_stop(const GameUpdateContext* ctx, MAYBE_UNUSED const u32 index) {
  if (ui_button(
          ctx->winCanvas,
          .label    = ui_shape_scratch(UiShape_Logout),
          .fontSize = 25,
          .tooltip  = loc_translate(GameId_MENU_EDIT_STOP_TOOLTIP))) {
    game_transition(ctx, GameState_MenuMain);
  }
}

ecs_view_define(ErrorView) {
  ecs_access_maybe_read(GapErrorComp);
  ecs_access_maybe_read(RendErrorComp);
}
ecs_view_define(TimeView) { ecs_access_write(SceneTimeComp); }

ecs_view_define(UpdateGlobalView) {
  ecs_access_maybe_read(SceneFactionStatsComp);
  ecs_access_maybe_write(DevStatsGlobalComp);
  ecs_access_read(SceneMissionComp);
  ecs_access_read(SceneTerrainComp);
  ecs_access_read(SceneTimeComp);
  ecs_access_write(AssetManagerComp);
  ecs_access_write(GameCmdComp);
  ecs_access_write(GameComp);
  ecs_access_write(GamePrefsComp);
  ecs_access_write(InputManagerComp);
  ecs_access_write(LocManagerComp);
  ecs_access_write(RendSettingsGlobalComp);
  ecs_access_write(SceneLevelManagerComp);
  ecs_access_write(SceneTimeSettingsComp);
  ecs_access_write(SceneVisibilityEnvComp);
  ecs_access_write(SndMixerComp);
  ecs_access_write(UiSettingsGlobalComp);
}

ecs_view_define(MainWindowView) {
  ecs_access_maybe_write(DevStatsComp);
  ecs_access_maybe_write(GameHudComp);
  ecs_access_maybe_write(GameInputComp);
  ecs_access_maybe_write(RendSettingsComp);
  ecs_access_write(GameMainWindowComp);
  ecs_access_write(GapWindowComp);
}

ecs_view_define(LevelView) {
  ecs_access_read(AssetComp);
  ecs_access_read(AssetLevelComp);
}

ecs_view_define(LevelRenderableView) {
  ecs_access_read(SceneRenderableComp);
  ecs_access_with(SceneLevelInstanceComp);
  ecs_access_maybe_read(SceneVisibilityComp);
}

ecs_view_define(RawAssetView) { ecs_access_read(AssetRawComp); }

ecs_view_define(UiCanvasView) {
  ecs_view_flags(EcsViewFlags_Exclusive); // Only access the canvas's we create.
  ecs_access_write(UiCanvasComp);
}

ecs_view_define(DevMenuView) { ecs_access_write(DevMenuComp); }
ecs_view_define(DevPanelView) { ecs_access_write(DevPanelComp); }
ecs_view_define(DevLevelPanelView) { ecs_access_write(DevLevelPanelComp); }

static void game_level_query_begin(const GameUpdateContext* ctx) {
  diag_assert(!ctx->game->levelLoadingMask);

  ctx->game->levelMask = 0;
  for (u32 i = 0; i != GameLevelsMax; ++i) {
    string_maybe_free(g_allocHeap, ctx->game->levelNames[i]);
    ctx->game->levelAssets[i] = 0;
    ctx->game->levelNames[i]  = string_empty;
  }

  const String levelPattern = string_lit("levels/game/*.level");
  EcsEntityId  queryAssets[asset_query_max_results];
  const u32    queryCount = asset_query(ctx->world, ctx->assets, levelPattern, queryAssets);

  for (u32 i = 0; i != math_min(queryCount, GameLevelsMax); ++i) {
    asset_acquire(ctx->world, queryAssets[i]);
    ctx->game->levelLoadingMask |= 1 << i;
    ctx->game->levelAssets[i] = queryAssets[i];
  }
}

static void game_level_query_update(const GameUpdateContext* ctx) {
  diag_assert(ctx->game->levelLoadingMask);

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
      name = string_lit("LEVEL_NAME_UNKNOWN");
    }
    ctx->game->levelMask |= 1 << idx;
    ctx->game->levelNames[idx] = string_dup(g_allocHeap, name);
  Done:
    asset_release(ctx->world, asset);
    ctx->game->levelLoadingMask &= ~(1 << idx);
  }
}

static void game_dev_panels_hide(const GameUpdateContext* ctx, const bool hidden) {
  diag_assert(ctx->devPanelView);
  for (EcsIterator* itr = ecs_view_itr(ctx->devPanelView); ecs_view_walk(itr);) {
    DevPanelComp* panel = ecs_view_write_t(itr, DevPanelComp);
    if (dev_panel_type(panel) != DevPanelType_Detached) {
      dev_panel_hide(panel, hidden);
    }
  }
}

static void game_dev_handle_level_requests(const GameUpdateContext* ctx) {
  diag_assert(ctx->devLevelPanelView);
  DevLevelRequest req;
  for (EcsIterator* itr = ecs_view_itr(ctx->devLevelPanelView); ecs_view_walk(itr);) {
    DevLevelPanelComp* levelPanel = ecs_view_write_t(itr, DevLevelPanelComp);
    if (dev_level_consume_request(levelPanel, &req)) {
      if (ctx->game->state == GameState_MenuMain || ctx->game->state == GameState_MenuSelect) {
        if (req.levelMode == SceneLevelMode_Edit) {
          ctx->game->flags |= GameFlags_EditMode;
        } else {
          ctx->game->flags &= ~GameFlags_EditMode;
        }
        game_transition(ctx, GameState_Loading);
        scene_level_load(ctx->world, req.levelMode, req.levelAsset);
      }
      break;
    }
  }
}

static bool game_level_ready(const GameUpdateContext* ctx) {
  if (!scene_level_loaded(ctx->levelManager)) {
    return false; // Still loading level.
  }
  const EcsEntityId terrainAsset = scene_level_terrain(ctx->levelManager);
  if (terrainAsset) {
    if (scene_terrain_resource_asset(ctx->terrain) != terrainAsset) {
      return false; // Terrain load hasn't started.
    }
    if (!scene_terrain_loaded(ctx->terrain)) {
      return false; // Still loading terrain.
    }
    const EcsEntityId terrainGraphic = scene_terrain_resource_graphic(ctx->terrain);
    if (!ecs_world_has_t(ctx->world, terrainGraphic, RendResFinishedComp)) {
      return false; // Still loading terrain renderer resource.
    }
  }
  for (EcsIterator* itr = ecs_view_itr(ctx->levelRenderableView); ecs_view_walk(itr);) {
    const SceneVisibilityComp* visComp = ecs_view_read_t(itr, SceneVisibilityComp);
    if (visComp && !scene_visible_for_render(ctx->visibilityEnv, visComp)) {
      continue; // Renderable not visible.
    }
    const SceneRenderableComp* renderable = ecs_view_read_t(itr, SceneRenderableComp);
    if (!ecs_world_has_t(ctx->world, renderable->graphic, RendResFinishedComp)) {
      return false; // Still loading renderer resources.
    }
  }
  return true;
}

ecs_system_define(GameUpdateSys) {
  EcsView*     globalView = ecs_world_view_t(world, UpdateGlobalView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (!globalItr) {
    return;
  }

  GameUpdateContext ctx = {
      .world               = world,
      .game                = ecs_view_write_t(globalItr, GameComp),
      .prefs               = ecs_view_write_t(globalItr, GamePrefsComp),
      .levelManager        = ecs_view_write_t(globalItr, SceneLevelManagerComp),
      .terrain             = ecs_view_read_t(globalItr, SceneTerrainComp),
      .input               = ecs_view_write_t(globalItr, InputManagerComp),
      .soundMixer          = ecs_view_write_t(globalItr, SndMixerComp),
      .locManager          = ecs_view_write_t(globalItr, LocManagerComp),
      .mission             = ecs_view_read_t(globalItr, SceneMissionComp),
      .factionStats        = ecs_view_read_t(globalItr, SceneFactionStatsComp),
      .time                = ecs_view_read_t(globalItr, SceneTimeComp),
      .timeSet             = ecs_view_write_t(globalItr, SceneTimeSettingsComp),
      .cmd                 = ecs_view_write_t(globalItr, GameCmdComp),
      .assets              = ecs_view_write_t(globalItr, AssetManagerComp),
      .visibilityEnv       = ecs_view_write_t(globalItr, SceneVisibilityEnvComp),
      .rendSetGlobal       = ecs_view_write_t(globalItr, RendSettingsGlobalComp),
      .uiSetGlobal         = ecs_view_write_t(globalItr, UiSettingsGlobalComp),
      .devStatsGlobal      = ecs_view_write_t(globalItr, DevStatsGlobalComp),
      .levelRenderableView = ecs_world_view_t(world, LevelRenderableView),
      .rawAssetView        = ecs_world_view_t(world, RawAssetView),
      .devPanelView        = ecs_world_view_t(world, DevPanelView),
      .devLevelPanelView   = ecs_world_view_t(world, DevLevelPanelView),
  };

  if (ctx.game->levelLoadingMask) {
    game_level_query_update(&ctx);
  } else if (ctx.game->flags & GameFlags_RefreshLevels) {
    game_level_query_begin(&ctx);
    ctx.game->flags &= ~GameFlags_RefreshLevels;
  }

  EcsIterator* canvasItr   = ecs_view_itr(ecs_world_view_t(world, UiCanvasView));
  EcsView*     mainWinView = ecs_world_view_t(world, MainWindowView);
  EcsIterator* mainWinItr  = ecs_view_maybe_at(mainWinView, ctx.game->mainWindow);
  if (mainWinItr) {
    ctx.winEntity    = ecs_view_entity(mainWinItr);
    ctx.winGame      = ecs_view_write_t(mainWinItr, GameMainWindowComp);
    ctx.winComp      = ecs_view_write_t(mainWinItr, GapWindowComp);
    ctx.winRendSet   = ecs_view_write_t(mainWinItr, RendSettingsComp);
    ctx.winHud       = ecs_view_write_t(mainWinItr, GameHudComp);
    ctx.winGameInput = ecs_view_write_t(mainWinItr, GameInputComp);
    ctx.winDevStats  = ecs_view_write_t(mainWinItr, DevStatsComp);
    if (ctx.winGame->devMenu) {
      ctx.winDevMenu = ecs_utils_write_t(world, DevMenuView, ctx.winGame->devMenu, DevMenuComp);
    }

    if (gap_window_events(ctx.winComp) & GapWindowEvents_Resized) {
      // Save last window size.
      const GapVector windowSize = gap_window_param(ctx.winComp, GapParam_WindowSize);
      ctx.prefs->fullscreen      = gap_window_mode(ctx.winComp) == GapWindowMode_Fullscreen;
      if (!ctx.prefs->fullscreen) {
        ctx.prefs->windowWidth  = windowSize.width;
        ctx.prefs->windowHeight = windowSize.height;
      }
      ctx.prefs->dirty = true;
      if (ctx.devStatsGlobal) {
        dev_stats_notify(
            ctx.devStatsGlobal,
            string_lit("WindowSize"),
            fmt_write_scratch("{}x{}", fmt_int(windowSize.width), fmt_int(windowSize.height)));
      }
    }

    if (input_triggered(ctx.input, GameId_Quit)) {
      game_quit(&ctx);
    }
    if (input_triggered(ctx.input, GameId_Fullscreen)) {
      game_fullscreen_toggle(&ctx);
    }

    if (ecs_view_maybe_jump(canvasItr, ctx.winGame->uiCanvas)) {
      ctx.winCanvas = ecs_view_write_t(canvasItr, UiCanvasComp);
      ui_canvas_reset(ctx.winCanvas);
    }

    if (ctx.game->stateNext) {
      game_transition(&ctx, ctx.game->stateNext);
      ctx.game->stateNext = GameState_None;
    } else {
      ++ctx.game->stateTicks;
    }
    if (ctx.game->flags & GameFlags_DevSupport) {
      game_dev_handle_level_requests(&ctx);
    }

    bool debugReq = false;
    debugReq |= ctx.winDevStats && dev_stats_debug(ctx.winDevStats) == DevStatDebug_On;
    debugReq |= ctx.game->state == GameState_Edit;

    if (debugReq && !(ctx.game->flags & GameFlags_DebugActive)) {
      game_dev_panels_hide(&ctx, false);
      scene_visibility_flags_set(ctx.visibilityEnv, SceneVisibilityFlags_ForceRender);
      input_blocker_update(ctx.input, InputBlocker_Debug, true);
      dev_stats_notify(ctx.devStatsGlobal, string_lit("Debug"), string_lit("On"));
      ctx.game->flags |= GameFlags_DebugActive;
    } else if (!debugReq && (ctx.game->flags & GameFlags_DebugActive)) {
      game_dev_panels_hide(&ctx, true);
      scene_visibility_flags_clear(ctx.visibilityEnv, SceneVisibilityFlags_ForceRender);
      input_blocker_update(ctx.input, InputBlocker_Debug, false);
      dev_stats_notify(ctx.devStatsGlobal, string_lit("Debug"), string_lit("Off"));
      ctx.game->flags &= ~GameFlags_DebugActive;
    }

    if (debugReq) {
      if (input_triggered(ctx.input, GameId_DevFreeCamera)) {
        game_toggle_camera(&ctx);
      }
      input_layer_enable(ctx.input, GameId_Dev);
      input_layer_disable(ctx.input, GameId_Game);
    } else {
      if (ctx.game->state == GameState_Play) {
        input_layer_enable(ctx.input, GameId_Game);
      } else {
        input_layer_disable(ctx.input, GameId_Game);
      }
      input_layer_disable(ctx.input, GameId_Dev);
    }

    MenuEntry menuEntries[32];
    u32       menuEntriesCount = 0;
    switch (ctx.game->state) {
    case GameState_None:
    case GameState_Count:
      break;
    case GameState_MenuMain: {
      menuEntries[menuEntriesCount++] = (MenuEntry){&menu_entry_play, .size = 1};
      if (ctx.devPanelView && asset_save_supported(ctx.assets)) {
        menuEntries[menuEntriesCount++] = (MenuEntry){&menu_entry_edit, .size = 1};
      }
      menuEntries[menuEntriesCount++] = (MenuEntry){&menu_entry_volume, .size = 1};
      menuEntries[menuEntriesCount++] = (MenuEntry){&menu_entry_vsync, .size = 1};
      menuEntries[menuEntriesCount++] = (MenuEntry){&menu_entry_limiter, .size = 1};
      menuEntries[menuEntriesCount++] = (MenuEntry){&menu_entry_quality, .size = 1};
      menuEntries[menuEntriesCount++] = (MenuEntry){&menu_entry_fullscreen, .size = 1};
      menuEntries[menuEntriesCount++] = (MenuEntry){&menu_entry_ui_scale, .size = 1};
      menuEntries[menuEntriesCount++] = (MenuEntry){&menu_entry_locale, .size = 1};
      menuEntries[menuEntriesCount++] = (MenuEntry){&menu_entry_credits, .size = 1};
      menuEntries[menuEntriesCount++] = (MenuEntry){&menu_entry_quit, .size = 1};
      menu_draw(&ctx, loc_translate(GameId_MENU_TITLE), 400, menuEntries, menuEntriesCount);
      menu_draw_version(&ctx);
    } break;
    case GameState_MenuSelect: {
      if (ctx.game->levelLoadingMask) {
        break; // Still loading the level list.
      }
      const u32 levelCount = bits_popcnt(ctx.game->levelMask);
      for (u32 i = 0; i != levelCount; ++i) {
        menuEntries[menuEntriesCount++] = (MenuEntry){&menu_entry_level, .size = 1};
      }
      if (ctx.devPanelView && asset_save_supported(ctx.assets)) {
        menuEntries[menuEntriesCount++] = (MenuEntry){&menu_entry_refresh_levels, .size = 1};
      }
      menuEntries[menuEntriesCount++] = (MenuEntry){&menu_entry_back, .size = 1};
      if (ctx.game->flags & GameFlags_EditMode) {
        menu_draw(&ctx, loc_translate(GameId_MENU_EDIT), 400, menuEntries, menuEntriesCount);
      } else {
        menu_draw(&ctx, loc_translate(GameId_MENU_PLAY), 400, menuEntries, menuEntriesCount);
      }
      menu_draw_version(&ctx);
    } break;
    case GameState_MenuCredits:
      menuEntries[menuEntriesCount++] = (MenuEntry){&menu_entry_credits_content, .size = 10};
      menuEntries[menuEntriesCount++] = (MenuEntry){&menu_entry_back, .size = 1};
      menu_draw(&ctx, loc_translate(GameId_MENU_CREDITS), 800, menuEntries, menuEntriesCount);
      menu_draw_version(&ctx);
      break;
    case GameState_Loading:
      menu_draw_spinner(&ctx);
      if (scene_level_error(ctx.levelManager)) {
        scene_level_error_clear(ctx.levelManager);
        game_transition_delayed(ctx.game, GameState_MenuMain);
        break;
      }
      if (game_level_ready(&ctx) && ctx.game->stateTicks >= GameLoadingMinTicks) {
        if (ctx.game->flags & GameFlags_EditMode) {
          game_transition_delayed(ctx.game, GameState_Edit);
        } else {
          game_transition_delayed(ctx.game, GameState_Play);
        }
        break;
      }
      break;
    case GameState_Play: {
      if (ctx.winHud && game_hud_consume_action(ctx.winHud, GameHudAction_Pause)) {
        game_transition_delayed(ctx.game, GameState_Pause);
      }
      const SceneMissionState missionState = scene_mission_state(ctx.mission);
      if (missionState == SceneMissionState_Success || missionState == SceneMissionState_Fail) {
        if (scene_mission_time_ended(ctx.mission, ctx.time) > time_seconds(2)) {
          game_transition_delayed(ctx.game, GameState_Result);
          const String resultSnd = string_lit("external/sound/builtin/mission-end-01.wav");
          game_sound_play(world, ctx.soundMixer, ctx.assets, resultSnd);
        }
      }
    } break;
    case GameState_Edit:
      menuEntries[menuEntriesCount++] = (MenuEntry){&menu_entry_edit_camera, .size = 1};
      menuEntries[menuEntriesCount++] = (MenuEntry){&menu_entry_edit_play, .size = 1};
      menuEntries[menuEntriesCount++] = (MenuEntry){&menu_entry_edit_discard, .size = 1};
      menuEntries[menuEntriesCount++] = (MenuEntry){&menu_entry_edit_save, .size = 1};
      menuEntries[menuEntriesCount++] = (MenuEntry){&menu_entry_edit_stop, .size = 1};
      menu_bar_draw(&ctx, menuEntries, menuEntriesCount);
      break;
    case GameState_Pause:
      menuEntries[menuEntriesCount++] = (MenuEntry){&menu_entry_resume, .size = 1};
      menuEntries[menuEntriesCount++] = (MenuEntry){&menu_entry_restart, .size = 1};
      if (ctx.devPanelView && asset_save_supported(ctx.assets)) {
        menuEntries[menuEntriesCount++] = (MenuEntry){&menu_entry_edit_current, .size = 1};
      }
      menuEntries[menuEntriesCount++] = (MenuEntry){&menu_entry_volume, .size = 1};
      menuEntries[menuEntriesCount++] = (MenuEntry){&menu_entry_exposure, .size = 1};
      menuEntries[menuEntriesCount++] = (MenuEntry){&menu_entry_vsync, .size = 1};
      menuEntries[menuEntriesCount++] = (MenuEntry){&menu_entry_limiter, .size = 1};
      menuEntries[menuEntriesCount++] = (MenuEntry){&menu_entry_quality, .size = 1};
      menuEntries[menuEntriesCount++] = (MenuEntry){&menu_entry_fullscreen, .size = 1};
      menuEntries[menuEntriesCount++] = (MenuEntry){&menu_entry_ui_scale, .size = 1};
      menuEntries[menuEntriesCount++] = (MenuEntry){&menu_entry_menu_main, .size = 1};
      menuEntries[menuEntriesCount++] = (MenuEntry){&menu_entry_quit, .size = 1};
      menu_draw(&ctx, loc_translate(GameId_MENU_PAUSED), 400, menuEntries, menuEntriesCount);
      menu_draw_version(&ctx);
      break;
    case GameState_Result: {
      const bool victory = scene_mission_state(ctx.mission) == SceneMissionState_Success;
      menuEntries[menuEntriesCount++] = (MenuEntry){&menu_entry_stat_time, .size = 1};
      menuEntries[menuEntriesCount++] = (MenuEntry){&menu_entry_stat_completed, .size = 1};
      menuEntries[menuEntriesCount++] = (MenuEntry){&menu_entry_stat_failed, .size = 1};
      menuEntries[menuEntriesCount++] = (MenuEntry){&menu_entry_stat_kills, .size = 1};
      menuEntries[menuEntriesCount++] = (MenuEntry){&menu_entry_stat_losses, .size = 1};
      menuEntries[menuEntriesCount++] = (MenuEntry){&menu_entry_restart, .size = 1};
      menuEntries[menuEntriesCount++] = (MenuEntry){&menu_entry_menu_main, .size = 1};
      menuEntries[menuEntriesCount++] = (MenuEntry){&menu_entry_quit, .size = 1};
      menu_draw(
          &ctx,
          victory ? loc_translate(GameId_MENU_VICTORY) : loc_translate(GameId_MENU_DEFEAT),
          400,
          menuEntries,
          menuEntriesCount);
      menu_draw_version(&ctx);
    } break;
    }
  }
}

typedef struct {
  bool devSupport;
} GameRegisterContext;

ecs_module_init(game_module) {
  const GameRegisterContext* ctx = ecs_init_ctx();

  ecs_register_comp(GameComp, .destructor = ecs_destruct_game_comp);
  ecs_register_comp(GameMainWindowComp);

  ecs_register_view(TimeView);
  ecs_register_view(ErrorView);
  ecs_register_view(UpdateGlobalView);
  ecs_register_view(MainWindowView);
  ecs_register_view(LevelView);
  ecs_register_view(LevelRenderableView);
  ecs_register_view(RawAssetView);
  ecs_register_view(UiCanvasView);

  if (ctx->devSupport) {
    ecs_register_view(DevPanelView);
    ecs_register_view(DevMenuView);
    ecs_register_view(DevLevelPanelView);
  }

  ecs_register_system(
      GameUpdateSys,
      ecs_view_id(UpdateGlobalView),
      ecs_view_id(MainWindowView),
      ecs_view_id(LevelView),
      ecs_view_id(UiCanvasView),
      ecs_view_id(LevelRenderableView),
      ecs_view_id(RawAssetView),
      ecs_view_id(DevPanelView),
      ecs_view_id(DevMenuView),
      ecs_view_id(DevLevelPanelView));

  ecs_order(GameUpdateSys, GameOrder_StateUpdate);
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

  const bool devSupport = cli_parse_provided(invoc, g_optDev);

  asset_register(def, &(AssetRegisterContext){.devSupport = devSupport});
  gap_register(def);
  input_register(def);
  loc_register(def);
  rend_register(def, &(RendRegisterContext){.enableStats = devSupport});
  scene_register(def, &(SceneRegisterContext){.devSupport = devSupport});
  snd_register(def);
  ui_register(def);
  vfx_register(def);
  if (devSupport) {
    dev_register(def);
  }

  ecs_register_module_ctx(def, game_module, &(GameRegisterContext){.devSupport = devSupport});
  ecs_register_module(def, game_cmd_module);
  ecs_register_module(def, game_hud_module);
  ecs_register_module(def, game_input_module);
  ecs_register_module(def, game_prefs_module);
}

static AssetManagerComp* game_init_assets(EcsWorld* world, const CliInvocation* invoc) {
  AssetManagerFlags flags = AssetManagerFlags_DelayUnload;
  if (cli_parse_provided(invoc, g_optDev)) {
    flags |= AssetManagerFlags_DevSupport;
  }
  const String overridePath = cli_read_string(invoc, g_optAssets, string_empty);
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

  loc_manager_init(world, prefs->locale);

  RendSettingsGlobalComp* rendSettingsGlobal = rend_settings_global_init(world, devSupport);
  UiSettingsGlobalComp*   uiSettingsGlobal   = ui_settings_global_init(world);

  SndMixerComp* soundMixer = snd_mixer_init(world);
  snd_mixer_gain_set(soundMixer, prefs->volume * 1e-2f);

  const EcsEntityId mainWin =
      game_window_create(world, assets, fullscreen, devSupport, width, height);
  RendSettingsComp* rendSettingsWin = rend_settings_window_init(world, mainWin);
  rendSettingsWin->flags |= RendFlags_2D;
  rendSettingsWin->exposure = game_exposure_value_get(prefs);

  game_quality_apply(prefs, rendSettingsGlobal, rendSettingsWin);
  game_ui_settings_apply(prefs, uiSettingsGlobal);

  GameFlags gameFlags = GameFlags_RefreshLevels;
  if (devSupport) {
    gameFlags |= GameFlags_DevSupport;
  }
  GameComp* game = ecs_world_add_t(
      world,
      ecs_world_global(world),
      GameComp,
      .flags       = gameFlags,
      .mainWindow  = mainWin,
      .musicHandle = sentinel_u32);

  game->creditsAsset = asset_lookup(world, assets, string_lit("credits.txt"));
  asset_acquire(world, game->creditsAsset);

  InputResourceComp* inputResource = input_resource_init(world);
  input_resource_load_map(inputResource, string_lit("global/global.inputs"));
  input_resource_load_map(inputResource, string_lit("global/game.inputs"));
  if (devSupport) {
    input_resource_load_map(inputResource, string_lit("global/dev.inputs"));
    input_resource_load_map(inputResource, string_lit("global/edit.inputs"));
  }

  scene_prefab_init(world, string_lit("global/game.prefabs"));
  scene_weapon_init(world, string_lit("global/game.weapons"));
  scene_product_init(world, string_lit("global/game.products"));

  const String level = cli_read_string(invoc, g_optLevel, string_empty);
  if (!string_is_empty(level)) {
    game_transition_delayed(game, GameState_Loading);
    scene_level_load(world, SceneLevelMode_Play, asset_lookup(world, assets, level));
  } else {
    game_transition_delayed(game, GameState_MenuMain);
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
