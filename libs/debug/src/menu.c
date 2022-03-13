#include "core_alloc.h"
#include "debug_menu.h"
#include "ecs_world.h"
#include "gap_window.h"
#include "rend_stats.h"
#include "scene_time.h"
#include "ui.h"

static const f32     g_debugStatsSmoothFactor = 0.1f;
static const UiColor g_debugWarnColor         = {255, 255, 0, 255};
static const UiColor g_debugErrorColor        = {255, 0, 0, 255};

typedef enum {
  DebugMenuFlags_ShowStats = 1 << 0,
} DebugMenuFlags;

typedef struct {
  TimeDuration updateTime, renderTime;
} DebugStats;

ecs_comp_define(DebugMenuComp) {
  EcsEntityId    window;
  DebugMenuFlags flags;
  DebugStats     stats;
  GapVector      lastWindowedSize;
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

static void debug_action_stats(DebugMenuComp* menu, UiCanvasComp* canvas) {
  const bool enabled = (menu->flags & DebugMenuFlags_ShowStats) != 0;
  if (ui_button(
          canvas,
          .label   = ui_shape_scratch(enabled ? UiShape_LayersClear : UiShape_Layers),
          .tooltip = enabled ? string_lit("Disable the statistics text")
                             : string_lit("Enable the statistics text"))) {
    menu->flags ^= DebugMenuFlags_ShowStats;
  }
}

static void debug_action_fullscreen(DebugMenuComp* menu, UiCanvasComp* canvas, GapWindowComp* win) {
  const bool active = gap_window_mode(win) == GapWindowMode_Fullscreen;
  if (ui_button(
          canvas,
          .label   = ui_shape_scratch(active ? UiShape_FullscreenExit : UiShape_Fullscreen),
          .tooltip = active ? string_lit("Exit fullscreen") : string_lit("Enter fullscreen"))) {
    if (active) {
      gap_window_resize(win, menu->lastWindowedSize, GapWindowMode_Windowed);
    } else {
      menu->lastWindowedSize = gap_window_param(win, GapParam_WindowSize);
      gap_window_resize(win, gap_vector(0, 0), GapWindowMode_Fullscreen);
    }
  }
}

static void debug_action_new_window(EcsWorld* world, UiCanvasComp* canvas) {
  static const GapVector newWindowSize = {1024, 768};

  if (ui_button(
          canvas,
          .label   = ui_shape_scratch(UiShape_OpenInNew),
          .tooltip = string_lit("Open a new window"))) {
    const EcsEntityId newWindow = gap_window_create(world, GapWindowFlags_Default, newWindowSize);
    debug_menu_create(world, newWindow);
  }
}

static void debug_action_close(UiCanvasComp* canvas, GapWindowComp* win) {
  if (ui_button(
          canvas,
          .label   = ui_shape_scratch(UiShape_Logout),
          .tooltip = string_lit("Close the window (Escape)"))) {
    gap_window_close(win);
  }
}

static void debug_action_bar_draw(
    EcsWorld* world, DebugMenuComp* menu, UiCanvasComp* canvas, GapWindowComp* win) {

  UiGridState grid = ui_grid_init(canvas, .align = UiAlign_TopRight, .size = {40, 40});

  debug_action_stats(menu, canvas);
  ui_grid_next_row(canvas, &grid);

  debug_action_fullscreen(menu, canvas, win);
  ui_grid_next_row(canvas, &grid);

  debug_action_new_window(world, canvas);
  ui_grid_next_row(canvas, &grid);

  debug_action_close(canvas, win);
}

static void debug_stats_draw_dur(
    DynString*         str,
    const TimeDuration dur,
    const String       label,
    const TimeDuration threshold1,
    const TimeDuration threshold2) {

  UiColor color = ui_color_white;
  if (dur > threshold2) {
    color = g_debugErrorColor;
  } else if (dur > threshold1) {
    color = g_debugWarnColor;
  }
  const f32 freq = 1.0f / (dur / (f32)time_second);
  fmt_write(
      str,
      "{}{<9} \ar{} ({}{}\ar hz)\n",
      fmt_ui_color(color),
      fmt_duration(dur),
      fmt_text(label),
      fmt_ui_color(color),
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
  debug_stats_draw_dur(&str, stats->updateTime, string_lit("update time"), time_milliseconds(17), time_milliseconds(17));
  debug_stats_draw_dur(&str, stats->renderTime, string_lit("render time"), time_milliseconds(10), time_milliseconds(17));
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
  ui_label(canvas, dynstring_view(&str), .align = UiAlign_TopLeft);
  ui_style_pop(canvas);
}

static void
debug_stats_update(DebugStats* stats, const RendStatsComp* rendStats, const SceneTimeComp* time) {
  stats->updateTime = debug_smooth_duration(stats->updateTime, time ? time->delta : time_second);
  stats->renderTime = debug_smooth_duration(stats->renderTime, rendStats->renderTime);
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
    GapWindowComp*       window    = ecs_view_write_t(windowItr, GapWindowComp);
    const RendStatsComp* rendStats = ecs_view_read_t(windowItr, RendStatsComp);

    if (time) {
      debug_stats_update(&menu->stats, rendStats, time);
    }

    ui_canvas_reset(canvas);
    if (menu->flags & DebugMenuFlags_ShowStats) {
      debug_stats_draw(canvas, &menu->stats, rendStats);
    }
    debug_action_bar_draw(world, menu, canvas, window);
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
