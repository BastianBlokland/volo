#include "core_alloc.h"
#include "debug_asset.h"
#include "debug_camera.h"
#include "debug_grid.h"
#include "debug_interface.h"
#include "debug_log_viewer.h"
#include "debug_menu.h"
#include "debug_physics.h"
#include "debug_rend.h"
#include "debug_stats.h"
#include "ecs_world.h"
#include "gap_window.h"
#include "input.h"
#include "ui.h"

// clang-format off

static const String g_tooltipStatsEnable     = string_static("Enable the \a.bStatistics\ar interface.");
static const String g_tooltipStatsDisable    = string_static("Disable the \a.bStatistics\ar interface.");
static const String g_tooltipPanelAsset      = string_static("Open the \a.bAsset Debug\ar panel.");
static const String g_tooltipPanelPhysics    = string_static("Open the \a.bPhysics Debug\ar panel.");
static const String g_tooltipPanelCamera     = string_static("Open the \a.bCamera settings\ar panel.");
static const String g_tooltipPanelGrid       = string_static("Open the \a.bGrid settings\ar panel.");
static const String g_tooltipPanelRend       = string_static("Open the \a.bRenderer settings\ar panel.");
static const String g_tooltipPanelInterface  = string_static("Open the \a.bInterface settings\ar panel.");
static const String g_tooltipFullscreenEnter = string_static("Enter fullscreen.");
static const String g_tooltipFullscreenExit  = string_static("Exit fullscreen.");
static const String g_tooltipWindowOpen      = string_static("Open a new window.");
static const String g_tooltipWindowClose     = string_static("Close the current window.");

// clang-format on

ecs_comp_define(DebugMenuComp) {
  EcsEntityId window;
  GapVector   lastWindowedSize;
  EcsEntityId panelAsset, panelPhysics, panelCamera, panelGrid, panelRend, panelInterface;
};

ecs_view_define(GlobalView) { ecs_access_write(InputManagerComp); }

ecs_view_define(MenuUpdateView) {
  ecs_access_write(DebugMenuComp);
  ecs_access_write(UiCanvasComp);
}

ecs_view_define(WindowUpdateView) {
  ecs_access_write(GapWindowComp);
  ecs_access_write(DebugStatsComp);
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
    DebugStatsComp*   stats,
    GapWindowComp*    win,
    const EcsEntityId winEntity) {

  UiTable table = ui_table(.align = UiAlign_TopRight, .rowHeight = 40);
  ui_table_add_column(&table, UiTableColumn_Fixed, 40);

  ui_table_next_row(canvas, &table);
  const bool statsEnabled = debug_stats_show(stats);
  if (ui_button(
          canvas,
          .label    = ui_shape_scratch(statsEnabled ? UiShape_LayersClear : UiShape_Layers),
          .fontSize = 30,
          .tooltip  = statsEnabled ? g_tooltipStatsDisable : g_tooltipStatsEnable)) {
    debug_stats_show_set(stats, !statsEnabled);
  }

  ui_table_next_row(canvas, &table);
  if (ui_button(
          canvas,
          .flags    = debug_panel_is_open(world, menu->panelAsset) ? UiWidget_Disabled : 0,
          .label    = ui_shape_scratch(UiShape_Storage),
          .fontSize = 30,
          .tooltip  = g_tooltipPanelAsset)) {
    debug_panel_open(world, &menu->panelAsset, winEntity, debug_asset_panel_open);
  }

  ui_table_next_row(canvas, &table);
  if (ui_button(
          canvas,
          .flags    = debug_panel_is_open(world, menu->panelPhysics) ? UiWidget_Disabled : 0,
          .label    = ui_shape_scratch(UiShape_ViewInAr),
          .fontSize = 30,
          .tooltip  = g_tooltipPanelPhysics)) {
    debug_panel_open(world, &menu->panelPhysics, winEntity, debug_physics_panel_open);
  }

  ui_table_next_row(canvas, &table);
  if (ui_button(
          canvas,
          .flags    = debug_panel_is_open(world, menu->panelCamera) ? UiWidget_Disabled : 0,
          .label    = ui_shape_scratch(UiShape_PhotoCamera),
          .fontSize = 30,
          .tooltip  = g_tooltipPanelCamera)) {
    debug_panel_open(world, &menu->panelCamera, winEntity, debug_camera_panel_open);
  }

  ui_table_next_row(canvas, &table);
  if (ui_button(
          canvas,
          .flags    = debug_panel_is_open(world, menu->panelGrid) ? UiWidget_Disabled : 0,
          .label    = ui_shape_scratch(UiShape_Grid4x4),
          .fontSize = 30,
          .tooltip  = g_tooltipPanelGrid)) {
    debug_panel_open(world, &menu->panelGrid, winEntity, debug_grid_panel_open);
  }

  ui_table_next_row(canvas, &table);
  if (ui_button(
          canvas,
          .flags    = debug_panel_is_open(world, menu->panelRend) ? UiWidget_Disabled : 0,
          .label    = ui_shape_scratch(UiShape_Brush),
          .fontSize = 30,
          .tooltip  = g_tooltipPanelRend)) {
    debug_panel_open(world, &menu->panelRend, winEntity, debug_rend_panel_open);
  }

  ui_table_next_row(canvas, &table);
  if (ui_button(
          canvas,
          .flags    = debug_panel_is_open(world, menu->panelInterface) ? UiWidget_Disabled : 0,
          .label    = ui_shape_scratch(UiShape_FormatShapes),
          .fontSize = 30,
          .tooltip  = g_tooltipPanelInterface)) {
    debug_panel_open(world, &menu->panelInterface, winEntity, debug_interface_panel_open);
  }

  ui_table_next_row(canvas, &table);
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

  ui_table_next_row(canvas, &table);
  if (ui_button(
          canvas,
          .label    = ui_shape_scratch(UiShape_OpenInNew),
          .fontSize = 30,
          .tooltip  = g_tooltipWindowOpen)) {
    static const GapVector g_newWindowSize = {1024, 768};
    const EcsEntityId newWindow = gap_window_create(world, GapWindowFlags_Default, g_newWindowSize);
    debug_menu_create(world, newWindow);
  }

  ui_table_next_row(canvas, &table);
  if (ui_button(
          canvas,
          .label    = ui_shape_scratch(UiShape_Logout),
          .fontSize = 30,
          .tooltip  = g_tooltipWindowClose)) {
    gap_window_close(win);
  }
}

ecs_system_define(DebugMenuUpdateSys) {
  EcsView*     globalView = ecs_world_view_t(world, GlobalView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (!globalItr) {
    return; // Global dependencies not initialized yet.
  }
  InputManagerComp* input = ecs_view_write_t(globalItr, InputManagerComp);

  // TODO: This does not belong here.
  if (input_triggered_lit(input, "InputCursorLock")) {
    input_cursor_mode_set(input, input_cursor_mode(input) ^ 1);
  }

  EcsView*     windowView = ecs_world_view_t(world, WindowUpdateView);
  EcsIterator* windowItr  = ecs_view_itr(windowView);

  EcsView* menuView = ecs_world_view_t(world, MenuUpdateView);
  for (EcsIterator* itr = ecs_view_itr(menuView); ecs_view_walk(itr);) {
    DebugMenuComp* menu   = ecs_view_write_t(itr, DebugMenuComp);
    UiCanvasComp*  canvas = ecs_view_write_t(itr, UiCanvasComp);

    if (!ecs_view_maybe_jump(windowItr, menu->window)) {
      continue;
    }
    GapWindowComp*  win   = ecs_view_write_t(windowItr, GapWindowComp);
    DebugStatsComp* stats = ecs_view_write_t(windowItr, DebugStatsComp);

    ui_canvas_reset(canvas);
    debug_action_bar_draw(world, canvas, menu, stats, win, menu->window);

    // TODO: This does not belong here.
    if (input_active_window(input) == menu->window && input_triggered_lit(input, "WindowClose")) {
      gap_window_close(win);
    }
  }
}

ecs_module_init(debug_menu_module) {
  ecs_register_comp(DebugMenuComp);

  ecs_register_view(GlobalView);
  ecs_register_view(MenuUpdateView);
  ecs_register_view(WindowUpdateView);

  ecs_register_system(
      DebugMenuUpdateSys,
      ecs_view_id(GlobalView),
      ecs_view_id(MenuUpdateView),
      ecs_view_id(WindowUpdateView));
}

EcsEntityId debug_menu_create(EcsWorld* world, const EcsEntityId window) {
  const EcsEntityId menuEntity = ui_canvas_create(world, window, UiCanvasCreateFlags_ToFront);
  ecs_world_add_t(world, menuEntity, DebugMenuComp, .window = window);

  debug_log_viewer_create(world, window);
  return menuEntity;
}
