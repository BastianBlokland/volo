#include "app/ecs.h"
#include "asset/manager.h"
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
#include "core/path.h"
#include "core/rng.h"
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
#include "rend/forward.h"
#include "rend/register.h"
#include "rend/settings.h"
#include "scene/camera.h"
#include "scene/level.h"
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
#include "ui/shape.h"
#include "ui/style.h"
#include "ui/widget.h"
#include "vfx/register.h"

#include "cmd.h"
#include "game.h"
#include "hud.h"
#include "input.h"
#include "prefs.h"

enum {
  GameLevelsMax       = 8,
  GameLoadingMinTicks = 5, // Not strictly needed, but avoids very short loading screen flashes.
};

ecs_comp_define(GameComp) {
  GameState state : 8;
  GameState statePrev : 8;
  GameState stateNext : 8;
  u32       stateTicks;
  bool      devSupport;
  bool      debugActive;

  EcsEntityId mainWindow;
  SndObjectId musicHandle;

  u32         levelMask;
  u32         levelLoadingMask;
  EcsEntityId levelAssets[GameLevelsMax];
  String      levelNames[GameLevelsMax];

  f32 prevExposure;
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
      [GameState_None]       = string_static("None"),
      [GameState_MenuMain]   = string_static("MenuMain"),
      [GameState_MenuSelect] = string_static("MenuSelect"),
      [GameState_Loading]    = string_static("Loading"),
      [GameState_Play]       = string_static("Play"),
      [GameState_Edit]       = string_static("Edit"),
      [GameState_Pause]      = string_static("Pause"),
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

  if (devSupport) {
    dev_log_viewer_create(world, window, LogMask_Info | LogMask_Warn | LogMask_Error);
  }

  const EcsEntityId uiCanvas = ui_canvas_create(world, window, UiCanvasCreateFlags_ToBack);
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
  EcsWorld*               world;
  GameComp*               game;
  GamePrefsComp*          prefs;
  SceneLevelManagerComp*  levelManager;
  const SceneTerrainComp* terrain;
  InputManagerComp*       input;
  SndMixerComp*           soundMixer;
  const SceneTimeComp*    time;
  SceneTimeSettingsComp*  timeSet;
  GameCmdComp*            cmd;
  AssetManagerComp*       assets;
  SceneVisibilityEnvComp* visibilityEnv;
  RendSettingsGlobalComp* rendSetGlobal;
  DevStatsGlobalComp*     devStatsGlobal;

  EcsEntityId         winEntity;
  GameMainWindowComp* winGame;
  GapWindowComp*      winComp;
  RendSettingsComp*   winRendSet;
  GameHudComp*        winHud;
  GameInputComp*      winGameInput;
  DevStatsComp*       winDevStats;
  UiCanvasComp*       winCanvas;

  EcsView* levelRenderableView;
  EcsView* devPanelView; // Null if dev-support is not enabled.
} GameUpdateContext;

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
    input_layer_disable(ctx->input, string_hash_lit("Game"));
    game_input_type_set(ctx->winGameInput, GameInputType_None);
    asset_loading_budget_set(ctx->assets, 0); // Infinite budget while not in gameplay.
    break;
  case GameState_Pause:
    ctx->timeSet->flags &= ~SceneTimeFlags_Paused;

    ctx->winRendSet->bloomIntensity = ctx->game->prevBloomIntensity;
    ctx->winRendSet->grayscaleFrac  = ctx->game->prevGrayscaleFrac;
    ctx->winRendSet->exposure       = ctx->game->prevExposure;
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
    input_layer_enable(ctx->input, string_hash_lit("Game"));
    asset_loading_budget_set(ctx->assets, time_milliseconds(2)); // Limit loading during gameplay.
    break;
  case GameState_Pause:
    ctx->timeSet->flags |= SceneTimeFlags_Paused;

    ctx->game->prevExposure   = ctx->winRendSet->exposure;
    ctx->winRendSet->exposure = 0.025f;

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
  ui_layout_push(ctx->winCanvas);
  ui_layout_set(ctx->winCanvas, ui_rect(ui_vector(0, 0), ui_vector(1, 1)), UiBase_Canvas);
  ui_layout_grow(ctx->winCanvas, UiAlign_MiddleCenter, ui_vector(-10, -5), UiBase_Absolute, Ui_XY);

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

typedef void (*MenuEntry)(const GameUpdateContext*, u32 index);

static void menu_draw(
    const GameUpdateContext* ctx, const String header, const MenuEntry entries[], const u32 count) {
  static const UiVector g_headerSize = {.x = 300.0f, .y = 75.0f};
  static const UiVector g_entrySize  = {.x = 300.0f, .y = 50.0f};
  static const f32      g_spacing    = 8.0f;

  ui_style_push(ctx->winCanvas);
  ui_style_transform(ctx->winCanvas, UiTransform_ToUpper);

  f32 totalHeight = (count - 1) * (g_entrySize.y + g_spacing);
  if (!string_is_empty(header)) {
    totalHeight += g_headerSize.y;
  }
  ui_layout_move_to(ctx->winCanvas, UiBase_Container, UiAlign_MiddleCenter, Ui_XY);
  ui_layout_move(ctx->winCanvas, ui_vector(0, totalHeight * 0.5f), UiBase_Absolute, Ui_Y);

  if (!string_is_empty(header)) {
    ui_layout_push(ctx->winCanvas);
    ui_layout_resize(ctx->winCanvas, UiAlign_MiddleCenter, g_headerSize, UiBase_Absolute, Ui_XY);

    ui_style_push(ctx->winCanvas);
    ui_style_outline(ctx->winCanvas, 5);
    ui_style_weight(ctx->winCanvas, UiWeight_Heavy);
    ui_style_color(ctx->winCanvas, ui_color(255, 173, 10, 255));
    ui_label(ctx->winCanvas, header, .align = UiAlign_MiddleCenter, .fontSize = 60);
    ui_style_pop(ctx->winCanvas);

    ui_layout_pop(ctx->winCanvas);
    ui_layout_move(ctx->winCanvas, ui_vector(0, -g_headerSize.y), UiBase_Absolute, Ui_Y);
  }

  ui_layout_resize(ctx->winCanvas, UiAlign_MiddleCenter, g_entrySize, UiBase_Absolute, Ui_XY);
  for (u32 i = 0; i != count; ++i) {
    entries[i](ctx, i);
    ui_layout_next(ctx->winCanvas, Ui_Down, g_spacing);
  }
  ui_style_pop(ctx->winCanvas);
}

static void menu_entry_play(const GameUpdateContext* ctx, MAYBE_UNUSED const u32 index) {
  if (ui_button(
          ctx->winCanvas,
          .label    = string_lit("Play"),
          .fontSize = 25,
          .tooltip  = string_lit("Go to level-select menu."))) {
    game_transition(ctx, GameState_MenuSelect);
  }
}

static void menu_entry_resume(const GameUpdateContext* ctx, MAYBE_UNUSED const u32 index) {
  if (ui_button(
          ctx->winCanvas,
          .label    = string_lit("Resume"),
          .fontSize = 25,
          .tooltip  = string_lit("Resume playing."),
          .activate = input_triggered_lit(ctx->input, "Pause"), )) {
    game_transition_delayed(ctx->game, GameState_Play);
  }
}

static void menu_entry_restart(const GameUpdateContext* ctx, MAYBE_UNUSED const u32 index) {
  if (ui_button(
          ctx->winCanvas,
          .label    = string_lit("Restart"),
          .fontSize = 25,
          .tooltip  = string_lit("Restart the current level."))) {
    game_transition(ctx, GameState_Loading);
    scene_level_reload(ctx->world, SceneLevelMode_Play);
  }
}

static void menu_entry_menu_main(const GameUpdateContext* ctx, MAYBE_UNUSED const u32 index) {
  if (ui_button(
          ctx->winCanvas,
          .label    = string_lit("Main-menu"),
          .fontSize = 25,
          .tooltip  = string_lit("Go back to the main-menu."))) {
    game_transition(ctx, GameState_MenuMain);
  }
}

static void menu_entry_volume(const GameUpdateContext* ctx, MAYBE_UNUSED const u32 index) {
  menu_draw_entry_frame(ctx);

  ui_layout_push(ctx->winCanvas);
  static const UiVector g_frameInset = {-40, -10};
  ui_layout_grow(ctx->winCanvas, UiAlign_MiddleCenter, g_frameInset, UiBase_Absolute, Ui_XY);
  ui_label(ctx->winCanvas, string_lit("Volume"));
  ui_layout_inner(
      ctx->winCanvas, UiBase_Current, UiAlign_MiddleRight, ui_vector(0.4f, 1), UiBase_Current);
  if (ui_slider(
          ctx->winCanvas,
          &ctx->prefs->volume,
          .max        = 1e2f,
          .step       = 1,
          .handleSize = 25,
          .thickness  = 10,
          .tooltip    = string_lit("Change the sound volume."))) {
    ctx->prefs->dirty = true;
    snd_mixer_gain_set(ctx->soundMixer, ctx->prefs->volume * 1e-2f);
  }
  ui_layout_pop(ctx->winCanvas);
}

static void menu_entry_powersaving(const GameUpdateContext* ctx, MAYBE_UNUSED const u32 index) {
  menu_draw_entry_frame(ctx);

  ui_layout_push(ctx->winCanvas);
  static const UiVector g_frameInset = {-40, -10};
  ui_layout_grow(ctx->winCanvas, UiAlign_MiddleCenter, g_frameInset, UiBase_Absolute, Ui_XY);
  ui_label(ctx->winCanvas, string_lit("Power saving"));
  if (ui_toggle(
          ctx->winCanvas,
          &ctx->prefs->powerSaving,
          .align   = UiAlign_MiddleRight,
          .size    = 25,
          .tooltip = string_lit("Save power by limiting the frame-rate to 30hz."))) {
    ctx->prefs->dirty = true;
    game_quality_apply(ctx->prefs, ctx->rendSetGlobal, ctx->winRendSet);
  }
  ui_layout_pop(ctx->winCanvas);
}

static void menu_entry_quality(const GameUpdateContext* ctx, MAYBE_UNUSED const u32 index) {
  menu_draw_entry_frame(ctx);

  ui_layout_push(ctx->winCanvas);
  static const UiVector g_frameInset = {-40, -10};
  ui_layout_grow(ctx->winCanvas, UiAlign_MiddleCenter, g_frameInset, UiBase_Absolute, Ui_XY);
  ui_label(ctx->winCanvas, string_lit("Quality"));
  ui_layout_inner(
      ctx->winCanvas, UiBase_Current, UiAlign_MiddleRight, ui_vector(0.4f, 0.6f), UiBase_Current);

  ui_style_push(ctx->winCanvas);
  ui_style_transform(ctx->winCanvas, UiTransform_None);

  i32* quality = (i32*)&ctx->prefs->quality;
  if (ui_select(
          ctx->winCanvas,
          quality,
          g_gameQualityLabels,
          GameQuality_Count,
          .tooltip = string_lit("Select the rendering quality."))) {
    ctx->prefs->dirty = true;
    game_quality_apply(ctx->prefs, ctx->rendSetGlobal, ctx->winRendSet);
  }

  ui_style_pop(ctx->winCanvas);
  ui_layout_pop(ctx->winCanvas);
}

static void menu_entry_fullscreen(const GameUpdateContext* ctx, MAYBE_UNUSED const u32 index) {
  menu_draw_entry_frame(ctx);

  ui_layout_push(ctx->winCanvas);
  static const UiVector g_frameInset = {-40, -10};
  ui_layout_grow(ctx->winCanvas, UiAlign_MiddleCenter, g_frameInset, UiBase_Absolute, Ui_XY);
  ui_label(ctx->winCanvas, string_lit("Fullscreen"));
  bool isFullscreen = gap_window_mode(ctx->winComp) == GapWindowMode_Fullscreen;
  if (ui_toggle(
          ctx->winCanvas,
          &isFullscreen,
          .align   = UiAlign_MiddleRight,
          .size    = 25,
          .tooltip = string_lit("Switch between fullscreen and windowed modes."))) {
    game_fullscreen_toggle(ctx);
  }
  ui_layout_pop(ctx->winCanvas);
}

static void menu_entry_quit(const GameUpdateContext* ctx, MAYBE_UNUSED const u32 index) {
  if (ui_button(
          ctx->winCanvas,
          .label    = string_lit("Quit"),
          .fontSize = 25,
          .tooltip  = string_lit("Quit to desktop."))) {
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
          .activate   = input_triggered_lit(ctx->input, "Back"),
          .tooltip    = string_lit("Back to previous menu."))) {
    game_transition(ctx, ctx->game->statePrev);
  }
  ui_layout_pop(ctx->winCanvas);
}

static void menu_entry_level(const GameUpdateContext* ctx, const u32 index) {
  const u32    levelIndex = (u32)bitset_index(bitset_from_var(ctx->game->levelMask), index);
  const String levelName  = ctx->game->levelNames[levelIndex];
  const String tooltip    = fmt_write_scratch("Play the '{}' level.", fmt_text(levelName));
  if (ui_button(ctx->winCanvas, .label = levelName, .fontSize = 25, .tooltip = tooltip)) {
    game_transition(ctx, GameState_Loading);
    scene_level_load(ctx->world, SceneLevelMode_Play, ctx->game->levelAssets[levelIndex]);
  }
}

ecs_view_define(ErrorView) {
  ecs_access_maybe_read(GapErrorComp);
  ecs_access_maybe_read(RendErrorComp);
}
ecs_view_define(TimeView) { ecs_access_write(SceneTimeComp); }

ecs_view_define(UpdateGlobalView) {
  ecs_access_read(SceneTerrainComp);
  ecs_access_read(SceneTimeComp);
  ecs_access_write(AssetManagerComp);
  ecs_access_write(GameCmdComp);
  ecs_access_write(GameComp);
  ecs_access_write(GamePrefsComp);
  ecs_access_write(InputManagerComp);
  ecs_access_write(RendSettingsGlobalComp);
  ecs_access_write(SceneLevelManagerComp);
  ecs_access_write(SceneTimeSettingsComp);
  ecs_access_write(SceneVisibilityEnvComp);
  ecs_access_write(SndMixerComp);
  ecs_access_maybe_write(DevStatsGlobalComp);
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
  ecs_access_with(SceneLevelInstanceComp);
  ecs_access_read(SceneRenderableComp);
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
      .time                = ecs_view_read_t(globalItr, SceneTimeComp),
      .timeSet             = ecs_view_write_t(globalItr, SceneTimeSettingsComp),
      .cmd                 = ecs_view_write_t(globalItr, GameCmdComp),
      .assets              = ecs_view_write_t(globalItr, AssetManagerComp),
      .visibilityEnv       = ecs_view_write_t(globalItr, SceneVisibilityEnvComp),
      .rendSetGlobal       = ecs_view_write_t(globalItr, RendSettingsGlobalComp),
      .devStatsGlobal      = ecs_view_write_t(globalItr, DevStatsGlobalComp),
      .levelRenderableView = ecs_world_view_t(world, LevelRenderableView),
      .devPanelView        = ecs_world_view_t(world, DevPanelView),
  };

  game_levels_query_update(&ctx);

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

    if (input_triggered_lit(ctx.input, "Quit")) {
      game_quit(&ctx);
    }
    if (input_triggered_lit(ctx.input, "Fullscreen")) {
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

    const bool debugReq = ctx.winDevStats && dev_stats_debug(ctx.winDevStats) == DevStatDebug_On;
    if (debugReq && !ctx.game->debugActive) {
      if (!ctx.winGame->devMenu) {
        ctx.winGame->devMenu = dev_menu_create(world, ctx.winEntity);
      }
      game_dev_panels_hide(&ctx, false);
      scene_visibility_flags_set(ctx.visibilityEnv, SceneVisibilityFlags_ForceRender);
      input_layer_enable(ctx.input, string_hash_lit("Dev"));
      if (ctx.winGameInput && input_triggered_lit(ctx.input, "DevFreeCamera")) {
        game_input_toggle_free_camera(ctx.winGameInput);
      }
      input_blocker_update(ctx.input, InputBlocker_Debug, true);
      dev_stats_notify(ctx.devStatsGlobal, string_lit("Debug"), string_lit("On"));
      ctx.game->debugActive = true;
    } else if (!debugReq && ctx.game->debugActive) {
      game_dev_panels_hide(&ctx, true);
      scene_visibility_flags_clear(ctx.visibilityEnv, SceneVisibilityFlags_ForceRender);
      input_layer_disable(ctx.input, string_hash_lit("Dev"));
      input_blocker_update(ctx.input, InputBlocker_Debug, false);
      dev_stats_notify(ctx.devStatsGlobal, string_lit("Debug"), string_lit("Off"));
      ctx.game->debugActive = false;
    }

    MenuEntry menuEntries[32];
    u32       menuEntriesCount = 0;
    switch (ctx.game->state) {
    case GameState_None:
    case GameState_Count:
      break;
    case GameState_MenuMain: {
      menuEntries[menuEntriesCount++] = &menu_entry_play;
      menuEntries[menuEntriesCount++] = &menu_entry_volume;
      menuEntries[menuEntriesCount++] = &menu_entry_powersaving;
      menuEntries[menuEntriesCount++] = &menu_entry_quality;
      menuEntries[menuEntriesCount++] = &menu_entry_fullscreen;
      menuEntries[menuEntriesCount++] = &menu_entry_quit;
      menu_draw(&ctx, string_lit("Volo"), menuEntries, menuEntriesCount);
      menu_draw_version(&ctx);
    } break;
    case GameState_MenuSelect: {
      const u32 levelCount = bits_popcnt(ctx.game->levelMask);
      for (u32 i = 0; i != levelCount; ++i) {
        menuEntries[menuEntriesCount++] = &menu_entry_level;
      }
      menuEntries[menuEntriesCount++] = &menu_entry_back;
      menu_draw(&ctx, string_lit("Play"), menuEntries, menuEntriesCount);
      menu_draw_version(&ctx);
    } break;
    case GameState_Loading:
      menu_draw_spinner(&ctx);
      if (scene_level_error(ctx.levelManager)) {
        scene_level_error_clear(ctx.levelManager);
        game_transition_delayed(ctx.game, GameState_MenuMain);
        break;
      }
      if (game_level_ready(&ctx) && ctx.game->stateTicks >= GameLoadingMinTicks) {
        game_transition_delayed(ctx.game, GameState_Play);
        break;
      }
      break;
    case GameState_Play:
    case GameState_Edit:
      if (ctx.winHud && game_hud_consume_action(ctx.winHud, GameHudAction_Pause)) {
        game_transition_delayed(ctx.game, GameState_Pause);
      }
      break;
    case GameState_Pause:
      menuEntries[menuEntriesCount++] = &menu_entry_resume;
      menuEntries[menuEntriesCount++] = &menu_entry_restart;
      menuEntries[menuEntriesCount++] = &menu_entry_volume;
      menuEntries[menuEntriesCount++] = &menu_entry_powersaving;
      menuEntries[menuEntriesCount++] = &menu_entry_quality;
      menuEntries[menuEntriesCount++] = &menu_entry_fullscreen;
      menuEntries[menuEntriesCount++] = &menu_entry_menu_main;
      menuEntries[menuEntriesCount++] = &menu_entry_quit;
      menu_draw(&ctx, string_lit("Pause"), menuEntries, menuEntriesCount);
      menu_draw_version(&ctx);
      break;
    }
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
  ecs_register_view(LevelRenderableView);
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
      ecs_view_id(LevelRenderableView),
      ecs_view_id(DevPanelView));

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
  rendSettingsWin->flags |= RendFlags_2D;

  game_quality_apply(prefs, rendSettingsGlobal, rendSettingsWin);

  GameComp* game = ecs_world_add_t(
      world,
      ecs_world_global(world),
      GameComp,
      .devSupport  = devSupport,
      .mainWindow  = mainWin,
      .musicHandle = sentinel_u32);

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
