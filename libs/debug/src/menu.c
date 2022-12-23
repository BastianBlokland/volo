#include "core_alloc.h"
#include "core_array.h"
#include "core_format.h"
#include "debug_animation.h"
#include "debug_asset.h"
#include "debug_brain.h"
#include "debug_camera.h"
#include "debug_ecs.h"
#include "debug_grid.h"
#include "debug_inspector.h"
#include "debug_interface.h"
#include "debug_light.h"
#include "debug_log_viewer.h"
#include "debug_menu.h"
#include "debug_prefab.h"
#include "debug_rend.h"
#include "debug_stats.h"
#include "debug_time.h"
#include "ecs_world.h"
#include "input.h"
#include "ui.h"

// clang-format off

static const String  g_tooltipStatsEnable     = string_static("Enable the \a.bStatistics\ar interface.");
static const String  g_tooltipStatsDisable    = string_static("Disable the \a.bStatistics\ar interface.");
static const String  g_tooltipPanelOpen       = string_static("Open the \a.b{}\ar panel.");
static const String  g_tooltipPanelClose      = string_static("Close the \a.b{}\ar panel.");
static const UiColor g_panelFrameColorNormal  = {32, 32, 32, 192};
static const UiColor g_panelFrameColorOpen    = {96, 96, 96, 255};

// clang-format on

typedef EcsEntityId (*PanelOpenFunc)(EcsWorld*, EcsEntityId);

static const struct {
  String        name;
  u32           iconShape;
  PanelOpenFunc openFunc;
  String        hotkeyName;
} g_debugPanelConfig[] = {
    {
        .name       = string_static("Inspector"),
        .iconShape  = UiShape_ViewInAr,
        .openFunc   = debug_inspector_panel_open,
        .hotkeyName = string_static("DebugPanelInspector"),
    },
    {
        .name       = string_static("Prefab"),
        .iconShape  = UiShape_Construction,
        .openFunc   = debug_prefab_panel_open,
        .hotkeyName = string_static("DebugPanelPrefab"),
    },
    {
        .name       = string_static("Time"),
        .iconShape  = UiShape_Timer,
        .openFunc   = debug_time_panel_open,
        .hotkeyName = string_static("DebugPanelTime"),
    },
    {
        .name       = string_static("Animation"),
        .iconShape  = UiShape_Animation,
        .openFunc   = debug_animation_panel_open,
        .hotkeyName = string_static("DebugPanelAnimation"),
    },
    {
        .name       = string_static("Brain"),
        .iconShape  = UiShape_Psychology,
        .openFunc   = debug_brain_panel_open,
        .hotkeyName = string_static("DebugPanelBrain"),
    },
    {
        .name       = string_static("Asset"),
        .iconShape  = UiShape_Storage,
        .openFunc   = debug_asset_panel_open,
        .hotkeyName = string_static("DebugPanelAsset"),
    },
    {
        .name       = string_static("Ecs"),
        .iconShape  = UiShape_Extension,
        .openFunc   = debug_ecs_panel_open,
        .hotkeyName = string_static("DebugPanelEcs"),
    },
    {
        .name       = string_static("Camera"),
        .iconShape  = UiShape_PhotoCamera,
        .openFunc   = debug_camera_panel_open,
        .hotkeyName = string_static("DebugPanelCamera"),
    },
    {
        .name       = string_static("Grid"),
        .iconShape  = UiShape_Grid4x4,
        .openFunc   = debug_grid_panel_open,
        .hotkeyName = string_static("DebugPanelGrid"),
    },
    {
        .name       = string_static("Renderer"),
        .iconShape  = UiShape_Brush,
        .openFunc   = debug_rend_panel_open,
        .hotkeyName = string_static("DebugPanelRenderer"),
    },
    {
        .name      = string_static("Light"),
        .iconShape = UiShape_Light,
        .openFunc  = debug_light_panel_open,
    },
    {
        .name      = string_static("Interface"),
        .iconShape = UiShape_FormatShapes,
        .openFunc  = debug_interface_panel_open,
    },
};

static String debug_panel_tooltip_scratch(const String panelName, const bool open) {
  return format_write_formatted_scratch(
      open ? g_tooltipPanelClose : g_tooltipPanelOpen, fmt_args(fmt_text(panelName)));
}

ecs_comp_define(DebugMenuComp) {
  EcsEntityId window;
  EcsEntityId panelEntities[array_elems(g_debugPanelConfig)];
};

ecs_view_define(GlobalView) {
  ecs_access_read(InputManagerComp);
  ecs_access_write(DebugStatsGlobalComp);
}

ecs_view_define(MenuUpdateView) {
  ecs_access_write(DebugMenuComp);
  ecs_access_write(UiCanvasComp);
}

ecs_view_define(WindowUpdateView) { ecs_access_write(DebugStatsComp); }

static bool debug_panel_is_open(EcsWorld* world, EcsEntityId panel) {
  return panel && ecs_world_exists(world, panel);
}

static void debug_notify_panel_state(
    DebugStatsGlobalComp* statsGlobal, const u32 panelIndex, const String state) {
  debug_stats_notify(
      statsGlobal,
      fmt_write_scratch("Panel {}", fmt_text(g_debugPanelConfig[panelIndex].name)),
      state);
}

static void debug_action_bar_draw(
    EcsWorld*               world,
    UiCanvasComp*           canvas,
    const InputManagerComp* input,
    DebugMenuComp*          menu,
    DebugStatsComp*         stats,
    DebugStatsGlobalComp*   statsGlobal,
    const EcsEntityId       winEntity) {

  UiTable table = ui_table(.align = UiAlign_TopRight, .rowHeight = 40);
  ui_table_add_column(&table, UiTableColumn_Fixed, 50);

  const bool windowActive = input_active_window(input) == winEntity;

  // Stats toggle.
  {
    ui_table_next_row(canvas, &table);
    const bool isEnabled = debug_stats_show(stats);

    const bool buttonPressed = ui_button(
        canvas,
        .label      = ui_shape_scratch(isEnabled ? UiShape_LayersClear : UiShape_Layers),
        .fontSize   = 30,
        .tooltip    = isEnabled ? g_tooltipStatsDisable : g_tooltipStatsEnable,
        .frameColor = isEnabled ? g_panelFrameColorOpen : g_panelFrameColorNormal);

    const bool hotkeyPressed = windowActive && input_triggered_lit(input, "DebugPanelStats");
    if (buttonPressed || hotkeyPressed) {
      debug_stats_show_set(stats, !isEnabled);
    }
  }

  // Panel open / close.
  for (u32 i = 0; i != array_elems(g_debugPanelConfig); ++i) {
    ui_table_next_row(canvas, &table);
    const bool isOpen = debug_panel_is_open(world, menu->panelEntities[i]);

    const bool buttonPressed = ui_button(
        canvas,
        .label      = ui_shape_scratch(g_debugPanelConfig[i].iconShape),
        .fontSize   = 30,
        .tooltip    = debug_panel_tooltip_scratch(g_debugPanelConfig[i].name, isOpen),
        .frameColor = isOpen ? g_panelFrameColorOpen : g_panelFrameColorNormal);

    const bool hotkeyPressed =
        windowActive && !string_is_empty(g_debugPanelConfig[i].hotkeyName) &&
        input_triggered_hash(input, string_hash(g_debugPanelConfig[i].hotkeyName));

    if (buttonPressed || hotkeyPressed) {
      if (isOpen) {
        ecs_world_entity_destroy(world, menu->panelEntities[i]);
        debug_notify_panel_state(statsGlobal, i, string_lit("closed"));
      } else {
        menu->panelEntities[i] = g_debugPanelConfig[i].openFunc(world, winEntity);
        debug_notify_panel_state(statsGlobal, i, string_lit("open"));
      }
    }
  }
}

ecs_system_define(DebugMenuUpdateSys) {
  EcsView*     globalView = ecs_world_view_t(world, GlobalView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (!globalItr) {
    return; // Global dependencies not initialized yet.
  }
  const InputManagerComp* input       = ecs_view_read_t(globalItr, InputManagerComp);
  DebugStatsGlobalComp*   statsGlobal = ecs_view_write_t(globalItr, DebugStatsGlobalComp);

  EcsView*     windowView = ecs_world_view_t(world, WindowUpdateView);
  EcsIterator* windowItr  = ecs_view_itr(windowView);

  EcsView* menuView = ecs_world_view_t(world, MenuUpdateView);
  for (EcsIterator* itr = ecs_view_itr(menuView); ecs_view_walk(itr);) {
    DebugMenuComp* menu   = ecs_view_write_t(itr, DebugMenuComp);
    UiCanvasComp*  canvas = ecs_view_write_t(itr, UiCanvasComp);

    if (!ecs_view_maybe_jump(windowItr, menu->window)) {
      continue;
    }
    DebugStatsComp* stats = ecs_view_write_t(windowItr, DebugStatsComp);

    ui_canvas_reset(canvas);
    debug_action_bar_draw(world, canvas, input, menu, stats, statsGlobal, menu->window);
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
