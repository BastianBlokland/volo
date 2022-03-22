#include "core_alloc.h"
#include "debug_camera.h"
#include "debug_grid.h"
#include "debug_interface.h"
#include "debug_menu.h"
#include "debug_rend.h"
#include "ecs_world.h"
#include "gap_window.h"
#include "rend_stats.h"
#include "scene_time.h"
#include "ui.h"

// clang-format off

static const String  g_tooltipStatsEnable     = string_static("Enable the statistics text.");
static const String  g_tooltipStatsDisable    = string_static("Disable the statistics text.");
static const String  g_tooltipPanelCamera     = string_static("Open the Camera settings panel.");
static const String  g_tooltipPanelGrid       = string_static("Open the Grid settings panel.");
static const String  g_tooltipPanelRend       = string_static("Open the Renderer settings panel.");
static const String  g_tooltipPanelInterface  = string_static("Open the Interface settings panel.");
static const String  g_tooltipFullscreenEnter = string_static("Enter fullscreen.");
static const String  g_tooltipFullscreenExit  = string_static("Exit fullscreen.");
static const String  g_tooltipWindowOpen      = string_static("Open a new window.");
static const String  g_tooltipWindowClose     = string_static("Close the current window (Escape).");
static const f32     g_debugStatsSmoothFactor = 0.1f;
static const UiColor g_debugWarnColor         = {255, 255, 0, 255};
static const UiColor g_debugErrorColor        = {255, 0, 0, 255};

// clang-format on

typedef enum {
  DebugMenuFlags_ShowStats = 1 << 0,
} DebugMenuFlags;

typedef struct {
  TimeDuration updateTime, limiterTime, renderTime, waitForRenderTime;
} DebugStats;

ecs_comp_define(DebugMenuComp) {
  EcsEntityId    window;
  DebugMenuFlags flags;
  DebugStats     stats;
  GapVector      lastWindowedSize;
  EcsEntityId    panelCamera, panelGrid, panelRend, panelInterface;
};

static TimeDuration debug_smooth_duration(const TimeDuration old, const TimeDuration new) {
  return (TimeDuration)((f64)old + ((f64)(new - old) * g_debugStatsSmoothFactor));
}

ecs_view_define(DebugGlobalView) { ecs_access_read(SceneTimeComp); }

ecs_view_define(MenuUpdateView) {
  ecs_access_write(DebugMenuComp);
  ecs_access_write(UiCanvasComp);
}

ecs_view_define(WindowUpdateView) {
  ecs_access_write(GapWindowComp);
  ecs_access_read(RendStatsComp);
}

static bool debug_panel_is_open(EcsWorld* world, EcsEntityId panel) {
  return panel && ecs_world_exists(world, panel);
}

static void debug_panel_open(
    EcsWorld*    world,
    EcsEntityId* reference,
    EcsEntityId  winEntity,
    EcsEntityId (*openFunc)(EcsWorld*, EcsEntityId)) {
  if (!debug_panel_is_open(world, *reference)) {
    *reference = openFunc(world, winEntity);
  }
}

static void debug_action_bar_draw(
    EcsWorld*         world,
    UiCanvasComp*     canvas,
    DebugMenuComp*    menu,
    GapWindowComp*    win,
    const EcsEntityId winEntity) {

  UiGridState grid = ui_grid_init(canvas, .align = UiAlign_TopRight, .size = {40, 40});

  const bool statsEnabled = (menu->flags & DebugMenuFlags_ShowStats) != 0;
  if (ui_button(
          canvas,
          .label    = ui_shape_scratch(statsEnabled ? UiShape_LayersClear : UiShape_Layers),
          .fontSize = 30,
          .tooltip  = statsEnabled ? g_tooltipStatsDisable : g_tooltipStatsEnable)) {
    menu->flags ^= DebugMenuFlags_ShowStats;
  }
  ui_grid_next_row(canvas, &grid);

  if (ui_button(
          canvas,
          .flags    = debug_panel_is_open(world, menu->panelCamera) ? UiWidget_Disabled : 0,
          .label    = ui_shape_scratch(UiShape_PhotoCamera),
          .fontSize = 30,
          .tooltip  = g_tooltipPanelCamera)) {
    debug_panel_open(world, &menu->panelCamera, winEntity, debug_camera_panel_open);
  }
  ui_grid_next_row(canvas, &grid);

  if (ui_button(
          canvas,
          .flags    = debug_panel_is_open(world, menu->panelGrid) ? UiWidget_Disabled : 0,
          .label    = ui_shape_scratch(UiShape_Grid4x4),
          .fontSize = 30,
          .tooltip  = g_tooltipPanelGrid)) {
    debug_panel_open(world, &menu->panelGrid, winEntity, debug_grid_panel_open);
  }
  ui_grid_next_row(canvas, &grid);

  if (ui_button(
          canvas,
          .flags    = debug_panel_is_open(world, menu->panelRend) ? UiWidget_Disabled : 0,
          .label    = ui_shape_scratch(UiShape_Brush),
          .fontSize = 30,
          .tooltip  = g_tooltipPanelRend)) {
    debug_panel_open(world, &menu->panelRend, winEntity, debug_rend_panel_open);
  }
  ui_grid_next_row(canvas, &grid);

  if (ui_button(
          canvas,
          .flags    = debug_panel_is_open(world, menu->panelInterface) ? UiWidget_Disabled : 0,
          .label    = ui_shape_scratch(UiShape_FormatShapes),
          .fontSize = 30,
          .tooltip  = g_tooltipPanelInterface)) {
    debug_panel_open(world, &menu->panelInterface, winEntity, debug_interface_panel_open);
  }
  ui_grid_next_row(canvas, &grid);

  const bool fullscreen = gap_window_mode(win) == GapWindowMode_Fullscreen;
  if (ui_button(
          canvas,
          .label    = ui_shape_scratch(fullscreen ? UiShape_FullscreenExit : UiShape_Fullscreen),
          .fontSize = 30,
          .tooltip  = fullscreen ? g_tooltipFullscreenExit : g_tooltipFullscreenEnter)) {
    if (fullscreen) {
      gap_window_resize(win, menu->lastWindowedSize, GapWindowMode_Windowed);
    } else {
      menu->lastWindowedSize = gap_window_param(win, GapParam_WindowSize);
      gap_window_resize(win, gap_vector(0, 0), GapWindowMode_Fullscreen);
    }
  }
  ui_grid_next_row(canvas, &grid);

  if (ui_button(
          canvas,
          .label    = ui_shape_scratch(UiShape_OpenInNew),
          .fontSize = 30,
          .tooltip  = g_tooltipWindowOpen)) {
    static const GapVector g_newWindowSize = {1024, 768};
    const EcsEntityId newWindow = gap_window_create(world, GapWindowFlags_Default, g_newWindowSize);
    debug_menu_create(world, newWindow);
  }
  ui_grid_next_row(canvas, &grid);

  if (ui_button(
          canvas,
          .label    = ui_shape_scratch(UiShape_Logout),
          .fontSize = 30,
          .tooltip  = g_tooltipWindowClose)) {
    gap_window_close(win);
  }
  ui_grid_next_row(canvas, &grid);
}

static void debug_stats_draw_dur(
    DynString*         str,
    const TimeDuration dur,
    const String       label,
    const TimeDuration threshold1,
    const TimeDuration threshold2) {

  FormatArg colorArg = fmt_nop();
  if (dur > threshold2) {
    colorArg = fmt_ui_color(g_debugErrorColor);
  } else if (dur > threshold1) {
    colorArg = fmt_ui_color(g_debugWarnColor);
  }
  const f32 freq = 1.0f / (dur / (f32)time_second);
  fmt_write(
      str,
      "{}{<9} \ar{} ({}{}\ar hz)\n",
      colorArg,
      fmt_duration(dur),
      fmt_text(label),
      colorArg,
      fmt_float(freq, .minDecDigits = 1, .maxDecDigits = 1));
}

static void
debug_stats_draw(UiCanvasComp* canvas, const DebugStats* stats, const RendStatsComp* rendStats) {
  static const UiVector g_textAreaSize  = {500, 500};
  static const f32      g_textEdgeSpace = 10;
  ui_layout_inner(canvas, UiBase_Container, UiAlign_TopLeft, g_textAreaSize, UiBase_Absolute);
  ui_layout_move_dir(canvas, Ui_Right, g_textEdgeSpace, UiBase_Absolute);
  ui_layout_move_dir(canvas, Ui_Down, g_textEdgeSpace, UiBase_Absolute);

  DynString str = dynstring_create(g_alloc_scratch, usize_kibibyte);

  // clang-format off
  fmt_write(&str, "{}\n", fmt_text(rendStats->gpuName));
  fmt_write(&str, "{<4}x{<4} pixels\n", fmt_int(rendStats->renderSize[0]), fmt_int(rendStats->renderSize[1]));
  debug_stats_draw_dur(&str, stats->updateTime, string_lit("update time"), time_milliseconds(17), time_milliseconds(18));
  fmt_write(&str, "{}{<9} \arlimiter time\n", stats->limiterTime < 0 ? fmt_ui_color(g_debugWarnColor) : fmt_nop(), fmt_duration(stats->limiterTime));
  debug_stats_draw_dur(&str, stats->renderTime, string_lit("render time"), time_milliseconds(10), time_milliseconds(17));
  fmt_write(&str, "{}{<9} \arrender wait time\n", stats->waitForRenderTime > time_millisecond ? fmt_ui_color(g_debugWarnColor) : fmt_nop(), fmt_duration(stats->waitForRenderTime));
  fmt_write(&str, "{<9} draws\n", fmt_int(rendStats->draws));
  fmt_write(&str, "{<9} instances\n", fmt_int(rendStats->instances));
  fmt_write(&str, "{<9} vertices\n", fmt_int(rendStats->vertices));
  fmt_write(&str, "{<9} triangles\n", fmt_int(rendStats->primitives));
  fmt_write(&str, "{<9} vertex shaders\n", fmt_int(rendStats->shadersVert));
  fmt_write(&str, "{<9} fragment shaders\n", fmt_int(rendStats->shadersFrag));
  fmt_write(&str, "{<9} memory-main\n", fmt_size(alloc_stats_total()));
  fmt_write(&str, "{<9} memory-renderer (reserved: {})\n", fmt_size(rendStats->ramOccupied), fmt_size(rendStats->ramReserved));
  fmt_write(&str, "{<9} memory-gpu (reserved: {})\n", fmt_size(rendStats->vramOccupied), fmt_size(rendStats->vramReserved));
  fmt_write(&str, "{<9} descriptor-sets (reserved: {})\n", fmt_int(rendStats->descSetsOccupied), fmt_int(rendStats->descSetsReserved));
  fmt_write(&str, "{<9} descriptor-layouts\n", fmt_int(rendStats->descLayouts));
  fmt_write(&str, "{<9} graphics\n", fmt_int(rendStats->resources[RendStatRes_Graphic]));
  fmt_write(&str, "{<9} shaders\n", fmt_int(rendStats->resources[RendStatRes_Shader]));
  fmt_write(&str, "{<9} meshes\n", fmt_int(rendStats->resources[RendStatRes_Mesh]));
  fmt_write(&str, "{<9} textures\n", fmt_int(rendStats->resources[RendStatRes_Texture]));
  // clang-format on

  ui_style_push(canvas);
  ui_style_outline(canvas, 2);
  ui_style_variation(canvas, UiVariation_Monospace);
  ui_label(canvas, dynstring_view(&str), .align = UiAlign_TopLeft);
  ui_style_pop(canvas);
}

static void
debug_stats_update(DebugStats* stats, const RendStatsComp* rendStats, const SceneTimeComp* time) {
  stats->updateTime  = debug_smooth_duration(stats->updateTime, time ? time->delta : time_second);
  stats->limiterTime = debug_smooth_duration(stats->limiterTime, rendStats->limiterTime);
  stats->renderTime  = debug_smooth_duration(stats->renderTime, rendStats->renderTime);
  stats->waitForRenderTime =
      debug_smooth_duration(stats->waitForRenderTime, rendStats->waitForRenderTime);
}

ecs_system_define(DebugMenuUpdateSys) {
  EcsView*             globalView = ecs_world_view_t(world, DebugGlobalView);
  EcsIterator*         globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  const SceneTimeComp* time       = globalItr ? ecs_view_read_t(globalItr, SceneTimeComp) : null;

  EcsView*     windowView = ecs_world_view_t(world, WindowUpdateView);
  EcsIterator* windowItr  = ecs_view_itr(windowView);

  EcsView* menuView = ecs_world_view_t(world, MenuUpdateView);
  for (EcsIterator* itr = ecs_view_itr(menuView); ecs_view_walk(itr);) {
    DebugMenuComp* menu   = ecs_view_write_t(itr, DebugMenuComp);
    UiCanvasComp*  canvas = ecs_view_write_t(itr, UiCanvasComp);

    if (!ecs_view_maybe_jump(windowItr, menu->window)) {
      continue;
    }
    GapWindowComp*       win       = ecs_view_write_t(windowItr, GapWindowComp);
    const EcsEntityId    winEntity = ecs_view_entity(windowItr);
    const RendStatsComp* rendStats = ecs_view_read_t(windowItr, RendStatsComp);

    if (time) {
      debug_stats_update(&menu->stats, rendStats, time);
    }

    ui_canvas_reset(canvas);
    if (menu->flags & DebugMenuFlags_ShowStats) {
      debug_stats_draw(canvas, &menu->stats, rendStats);
    }
    debug_action_bar_draw(world, canvas, menu, win, winEntity);
  }
}

ecs_module_init(debug_menu_module) {
  ecs_register_comp(DebugMenuComp);

  ecs_register_view(DebugGlobalView);
  ecs_register_view(MenuUpdateView);
  ecs_register_view(WindowUpdateView);

  ecs_register_system(
      DebugMenuUpdateSys,
      ecs_view_id(DebugGlobalView),
      ecs_view_id(MenuUpdateView),
      ecs_view_id(WindowUpdateView));
}

EcsEntityId debug_menu_create(EcsWorld* world, const EcsEntityId window) {
  const EcsEntityId menuEntity = ui_canvas_create(world, window);
  ecs_world_add_t(
      world, menuEntity, DebugMenuComp, .window = window, .flags = DebugMenuFlags_ShowStats);
  return menuEntity;
}
