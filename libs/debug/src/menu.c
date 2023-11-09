#include "core_alloc.h"
#include "core_array.h"
#include "core_format.h"
#include "debug_animation.h"
#include "debug_asset.h"
#include "debug_camera.h"
#include "debug_ecs.h"
#include "debug_grid.h"
#include "debug_inspector.h"
#include "debug_interface.h"
#include "debug_level.h"
#include "debug_menu.h"
#include "debug_prefab.h"
#include "debug_rend.h"
#include "debug_script.h"
#include "debug_sound.h"
#include "debug_stats.h"
#include "debug_time.h"
#include "ecs_utils.h"
#include "ecs_world.h"
#include "input.h"
#include "scene_lifetime.h"
#include "ui.h"

// clang-format off

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
        .name       = string_static("Level"),
        .iconShape  = UiShape_Globe,
        .openFunc   = debug_level_panel_open,
        .hotkeyName = string_static("DebugPanelLevel"),
    },
    {
        .name       = string_static("Sound"),
        .iconShape  = UiShape_MusicNote,
        .openFunc   = debug_sound_panel_open,
        .hotkeyName = string_static("DebugPanelSound"),
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
        .name       = string_static("Script"),
        .iconShape  = UiShape_Description,
        .openFunc   = debug_script_panel_open,
        .hotkeyName = string_static("DebugPanelScript"),
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
        .name      = string_static("Grid"),
        .iconShape = UiShape_Grid4x4,
        .openFunc  = debug_grid_panel_open,
    },
    {
        .name       = string_static("Renderer"),
        .iconShape  = UiShape_Brush,
        .openFunc   = debug_rend_panel_open,
        .hotkeyName = string_static("DebugPanelRenderer"),
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
ecs_view_define(CanvasView) { ecs_access_read(UiCanvasComp); }

static bool debug_panel_is_open(EcsWorld* world, const EcsEntityId panel) {
  return panel && ecs_world_exists(world, panel);
}

static void debug_notify_panel_state(
    DebugStatsGlobalComp* statsGlobal, const u32 panelIndex, const String state) {
  debug_stats_notify(
      statsGlobal,
      fmt_write_scratch("Panel {}", fmt_text(g_debugPanelConfig[panelIndex].name)),
      state);
}

static EcsEntityId debug_panel_topmost(EcsWorld* world, const DebugMenuComp* menu) {
  EcsEntityId topmost      = 0;
  i32         topmostOrder = i32_min;
  array_for_t(menu->panelEntities, EcsEntityId, panelEntity) {
    if (debug_panel_is_open(world, *panelEntity)) {
      const UiCanvasComp* canvas = ecs_utils_read_t(world, CanvasView, *panelEntity, UiCanvasComp);
      if (ui_canvas_order(canvas) >= topmostOrder) {
        topmost      = *panelEntity;
        topmostOrder = ui_canvas_order(canvas);
      }
    }
  }
  return topmost;
}

static void debug_action_bar_draw(
    EcsWorld*               world,
    const EcsEntityId       menuEntity,
    UiCanvasComp*           canvas,
    const InputManagerComp* input,
    DebugMenuComp*          menu,
    DebugStatsGlobalComp*   statsGlobal,
    const EcsEntityId       winEntity) {

  UiTable table = ui_table(.align = UiAlign_TopRight, .rowHeight = 35);
  ui_table_add_column(&table, UiTableColumn_Fixed, 45);

  const bool windowActive = input_active_window(input) == winEntity;
  const u32  rows         = 1 /* Icon */ + array_elems(g_debugPanelConfig) /* Panels */;
  ui_table_draw_bg(canvas, &table, rows, ui_color(178, 0, 0, 192));

  ui_table_next_row(canvas, &table);
  ui_canvas_draw_glyph(canvas, UiShape_Bug, 0, UiFlags_Interactable);

  // Panel open / close.
  for (u32 i = 0; i != array_elems(g_debugPanelConfig); ++i) {
    ui_table_next_row(canvas, &table);
    const bool isOpen = debug_panel_is_open(world, menu->panelEntities[i]);

    const bool hotkeyPressed =
        windowActive && !string_is_empty(g_debugPanelConfig[i].hotkeyName) &&
        input_triggered_hash(input, string_hash(g_debugPanelConfig[i].hotkeyName));

    if (ui_button(
            canvas,
            .label      = ui_shape_scratch(g_debugPanelConfig[i].iconShape),
            .fontSize   = 25,
            .tooltip    = debug_panel_tooltip_scratch(g_debugPanelConfig[i].name, isOpen),
            .frameColor = isOpen ? g_panelFrameColorOpen : g_panelFrameColorNormal,
            .activate   = hotkeyPressed)) {

      if (isOpen) {
        ecs_world_entity_destroy(world, menu->panelEntities[i]);
        debug_notify_panel_state(statsGlobal, i, string_lit("closed"));
      } else {
        const EcsEntityId panelEntity = g_debugPanelConfig[i].openFunc(world, winEntity);
        ecs_world_add_t(world, panelEntity, SceneLifetimeOwnerComp, .owners[0] = menuEntity);
        menu->panelEntities[i] = panelEntity;
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

  EcsView* menuView = ecs_world_view_t(world, MenuUpdateView);
  for (EcsIterator* itr = ecs_view_itr(menuView); ecs_view_walk(itr);) {
    const EcsEntityId menuEntity = ecs_view_entity(itr);
    DebugMenuComp*    menu       = ecs_view_write_t(itr, DebugMenuComp);
    UiCanvasComp*     canvas     = ecs_view_write_t(itr, UiCanvasComp);

    ui_canvas_reset(canvas);
    debug_action_bar_draw(world, menuEntity, canvas, input, menu, statsGlobal, menu->window);

    if (input_triggered_lit(input, "DebugPanelClose")) {
      const EcsEntityId topmostPanel = debug_panel_topmost(world, menu);
      if (topmostPanel) {
        ui_canvas_sound(canvas, UiSoundType_ClickAlt);
        ecs_world_entity_destroy(world, topmostPanel);
      }
    }
  }
}

ecs_module_init(debug_menu_module) {
  ecs_register_comp(DebugMenuComp);

  ecs_register_view(GlobalView);
  ecs_register_view(MenuUpdateView);
  ecs_register_view(CanvasView);

  ecs_register_system(
      DebugMenuUpdateSys,
      ecs_view_id(GlobalView),
      ecs_view_id(MenuUpdateView),
      ecs_view_id(CanvasView));
}

EcsEntityId debug_menu_create(EcsWorld* world, const EcsEntityId window) {
  const EcsEntityId menuEntity = ui_canvas_create(world, window, UiCanvasCreateFlags_ToFront);
  ecs_world_add_t(world, menuEntity, DebugMenuComp, .window = window);
  return menuEntity;
}
