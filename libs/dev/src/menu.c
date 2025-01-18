#include "core_alloc.h"
#include "core_array.h"
#include "core_dynstring.h"
#include "core_format.h"
#include "core_math.h"
#include "dev_asset.h"
#include "dev_camera.h"
#include "dev_ecs.h"
#include "dev_grid.h"
#include "dev_inspector.h"
#include "dev_interface.h"
#include "dev_level.h"
#include "dev_menu.h"
#include "dev_panel.h"
#include "dev_prefab.h"
#include "dev_rend.h"
#include "dev_script.h"
#include "dev_skeleton.h"
#include "dev_sound.h"
#include "dev_stats.h"
#include "dev_time.h"
#include "dev_trace.h"
#include "dev_vfx.h"
#include "ecs_utils.h"
#include "ecs_view.h"
#include "ecs_world.h"
#include "gap_window.h"
#include "input_manager.h"
#include "rend_settings.h"
#include "scene_lifetime.h"
#include "ui_canvas.h"
#include "ui_shape.h"
#include "ui_table.h"
#include "ui_widget.h"

// clang-format off

static const String  g_menuChildTooltipOpen       = string_static("Open the \a.b{}\ar panel.");
static const String  g_menuChildTooltipClose      = string_static("Close the \a.b{}\ar panel.");
static const String  g_menuChildTooltipDetach     = string_static("\a.bNote:\ar Hold \a.bControl\ar while clicking to open it detached.");
static const UiColor g_menuChildFrameColorNormal  = {32, 32, 32, 192};
static const UiColor g_menuChildFrameColorOpen    = {96, 96, 96, 255};

// clang-format on

typedef EcsEntityId (*ChildOpenFunc)(EcsWorld*, EcsEntityId, DevPanelType);

static const struct {
  String        name;
  u32           iconShape;
  bool          autoOpen;
  GapVector     detachedSize;
  ChildOpenFunc openFunc;
  String        hotkeyName;
} g_menuChildConfig[] = {
    {
        .name         = string_static("Inspector"),
        .iconShape    = UiShape_ViewInAr,
        .detachedSize = {.x = 500, .y = 500},
        .openFunc     = dev_inspector_panel_open,
        .hotkeyName   = string_static("DevPanelInspector"),
        .autoOpen     = true,
    },
    {
        .name         = string_static("Prefab"),
        .iconShape    = UiShape_Construction,
        .detachedSize = {.x = 500, .y = 350},
        .openFunc     = dev_prefab_panel_open,
        .hotkeyName   = string_static("DevPanelPrefab"),
        .autoOpen     = true,
    },
    {
        .name         = string_static("Level"),
        .iconShape    = UiShape_Globe,
        .detachedSize = {.x = 500, .y = 300},
        .openFunc     = dev_level_panel_open,
        .hotkeyName   = string_static("DevPanelLevel"),
    },
    {
        .name         = string_static("Sound"),
        .iconShape    = UiShape_MusicNote,
        .detachedSize = {.x = 800, .y = 685},
        .openFunc     = dev_sound_panel_open,
        .hotkeyName   = string_static("DevPanelSound"),
    },
    {
        .name         = string_static("Time"),
        .iconShape    = UiShape_Timer,
        .detachedSize = {.x = 500, .y = 250},
        .openFunc     = dev_time_panel_open,
        .hotkeyName   = string_static("DevPanelTime"),
    },
    {
        .name         = string_static("Skeleton"),
        .iconShape    = UiShape_Body,
        .detachedSize = {.x = 950, .y = 350},
        .openFunc     = dev_skeleton_panel_open,
        .hotkeyName   = string_static("DevPanelSkeleton"),
    },
    {
        .name         = string_static("Script"),
        .iconShape    = UiShape_Description,
        .detachedSize = {.x = 800, .y = 600},
        .openFunc     = dev_script_panel_open,
        .hotkeyName   = string_static("DevPanelScript"),
    },
    {
        .name         = string_static("Asset"),
        .iconShape    = UiShape_Storage,
        .detachedSize = {.x = 950, .y = 500},
        .openFunc     = dev_asset_panel_open,
        .hotkeyName   = string_static("DevPanelAsset"),
    },
    {
        .name         = string_static("Ecs"),
        .iconShape    = UiShape_Extension,
        .detachedSize = {.x = 800, .y = 500},
        .openFunc     = dev_ecs_panel_open,
        .hotkeyName   = string_static("DevPanelEcs"),
    },
    {
        .name         = string_static("Trace"),
        .iconShape    = UiShape_QueryStats,
        .detachedSize = {.x = 800, .y = 500},
        .openFunc     = dev_trace_panel_open,
        .hotkeyName   = string_static("DevPanelTrace"),
    },
    {
        .name         = string_static("Camera"),
        .iconShape    = UiShape_PhotoCamera,
        .detachedSize = {.x = 500, .y = 400},
        .openFunc     = dev_camera_panel_open,
    },
    {
        .name         = string_static("Grid"),
        .iconShape    = UiShape_Grid4x4,
        .detachedSize = {.x = 500, .y = 220},
        .openFunc     = dev_grid_panel_open,
    },
    {
        .name         = string_static("Renderer"),
        .iconShape    = UiShape_Brush,
        .detachedSize = {.x = 800, .y = 520},
        .openFunc     = dev_rend_panel_open,
        .hotkeyName   = string_static("DevPanelRenderer"),
    },
    {
        .name         = string_static("Vfx"),
        .iconShape    = UiShape_Diamond,
        .detachedSize = {.x = 850, .y = 500},
        .openFunc     = dev_vfx_panel_open,
    },
    {
        .name         = string_static("Interface"),
        .iconShape    = UiShape_FormatShapes,
        .detachedSize = {.x = 500, .y = 190},
        .openFunc     = dev_interface_panel_open,
    },
};

static String
menu_child_tooltip_scratch(const u32 childIndex, const bool open, const bool allowDetach) {
  Mem       scratchMem = alloc_alloc(g_allocScratch, 1024, 1);
  DynString str        = dynstring_create_over(scratchMem);

  format_write_formatted(
      &str,
      open ? g_menuChildTooltipClose : g_menuChildTooltipOpen,
      fmt_args(fmt_text(g_menuChildConfig[childIndex].name)));

  if (!open && allowDetach) {
    dynstring_append_char(&str, '\n');
    dynstring_append(&str, g_menuChildTooltipDetach);
  }

  return dynstring_view(&str);
}

ecs_comp_define(DevMenuComp) {
  EcsEntityId window;
  EcsEntityId childEntities[array_elems(g_menuChildConfig)];
};

ecs_view_define(GlobalView) {
  ecs_access_read(InputManagerComp);
  ecs_access_write(DebugStatsGlobalComp);
}
ecs_view_define(PanelUpdateView) {
  ecs_view_flags(EcsViewFlags_Exclusive); // DevMenuComp's are exclusively managed here.

  ecs_access_read(DevPanelComp);
  ecs_access_write(DevMenuComp);
  ecs_access_write(UiCanvasComp);
}
ecs_view_define(CanvasView) { ecs_access_read(UiCanvasComp); }
ecs_view_define(WindowView) { ecs_access_read(GapWindowComp); }

static void menu_notify_child_state(
    DebugStatsGlobalComp* statsGlobal, const u32 childIndex, const String state) {
  debug_stats_notify(
      statsGlobal,
      fmt_write_scratch("Panel {}", fmt_text(g_menuChildConfig[childIndex].name)),
      state);
}

static bool menu_child_is_open(EcsWorld* world, const DevMenuComp* menu, const u32 childIndex) {
  const EcsEntityId childEntity = menu->childEntities[childIndex];
  return childEntity && ecs_world_exists(world, childEntity);
}

static void menu_child_open(
    EcsWorld* world, DevMenuComp* menu, const EcsEntityId menuEntity, const u32 childIndex) {
  const DevPanelType type  = DevPanelType_Normal;
  const EcsEntityId  panel = g_menuChildConfig[childIndex].openFunc(world, menu->window, type);
  ecs_world_add_t(world, panel, SceneLifetimeOwnerComp, .owners[0] = menuEntity);
  menu->childEntities[childIndex] = panel;
}

static void menu_child_open_detached(
    EcsWorld*         world,
    UiCanvasComp*     canvas,
    DevMenuComp*      menu,
    const EcsEntityId menuEntity,
    const u32         childIndex) {
  const f32 scale = ui_canvas_scale(canvas);

  GapVector size = g_menuChildConfig[childIndex].detachedSize;

  size = (GapVector){
      .x = (i32)math_round_up_f32((size.x ? size.x : 500) * scale),
      .y = (i32)math_round_up_f32((size.y ? size.y : 500) * scale),
  };

  const GapWindowMode  mode           = GapWindowMode_Windowed;
  const GapWindowFlags flags          = GapWindowFlags_CloseOnRequest;
  const GapIcon        icon           = GapIcon_Tool;
  const String         title          = g_menuChildConfig[childIndex].name;
  const EcsEntityId    detachedWindow = gap_window_create(world, mode, flags, size, icon, title);
  RendSettingsComp*    rendSettings   = rend_settings_window_init(world, detachedWindow);

  // No vsync on the detached window to reduce impact on the rendering of the main window.
  rendSettings->flags       = 0;
  rendSettings->presentMode = RendPresentMode_Immediate;

  const DevPanelType type  = DevPanelType_Detached;
  const EcsEntityId  panel = g_menuChildConfig[childIndex].openFunc(world, detachedWindow, type);

  ecs_world_add_t(world, detachedWindow, SceneLifetimeOwnerComp, .owners[0] = panel);
  ecs_world_add_t(world, panel, SceneLifetimeOwnerComp, .owners[0] = menuEntity);

  menu->childEntities[childIndex] = panel;
}

static EcsEntityId menu_child_topmost(EcsWorld* world, const DevMenuComp* menu) {
  EcsEntityId topmost      = 0;
  i32         topmostOrder = i32_min;
  for (u32 childIndex = 0; childIndex != array_elems(menu->childEntities); ++childIndex) {
    if (menu_child_is_open(world, menu, childIndex)) {
      const EcsEntityId   childEntity = menu->childEntities[childIndex];
      const UiCanvasComp* canvas = ecs_utils_read_t(world, CanvasView, childEntity, UiCanvasComp);
      if (ui_canvas_order(canvas) >= topmostOrder) {
        topmost      = childEntity;
        topmostOrder = ui_canvas_order(canvas);
      }
    }
  }
  return topmost;
}

static bool menu_child_hotkey_pressed(const InputManagerComp* input, const u32 childIndex) {
  const String hotkeyName = g_menuChildConfig[childIndex].hotkeyName;
  if (string_is_empty(hotkeyName)) {
    return false;
  }
  return input_triggered_hash(input, string_hash(hotkeyName));
}

static void menu_action_bar_draw(
    EcsWorld*               world,
    const EcsEntityId       menuEntity,
    UiCanvasComp*           canvas,
    const InputManagerComp* input,
    DevMenuComp*            menu,
    DebugStatsGlobalComp*   statsGlobal,
    const EcsEntityId       winEntity,
    const GapWindowComp*    win) {

  UiTable table = ui_table(.align = UiAlign_TopRight, .rowHeight = 35);
  ui_table_add_column(&table, UiTableColumn_Fixed, 45);

  const bool allowDetach = gap_window_mode(win) == GapWindowMode_Windowed;

  const bool windowActive = input_active_window(input) == winEntity;
  const u32  rows         = 1 /* Icon */ + array_elems(g_menuChildConfig) /* Panels */;
  ui_table_draw_bg(canvas, &table, rows, ui_color(178, 0, 0, 192));

  ui_table_next_row(canvas, &table);
  ui_canvas_draw_glyph(canvas, UiShape_Construction, 0, UiFlags_Interactable);

  // Panel open / close.
  for (u32 childIndex = 0; childIndex != array_elems(g_menuChildConfig); ++childIndex) {
    ui_table_next_row(canvas, &table);
    const bool isOpen = menu_child_is_open(world, menu, childIndex);

    if (ui_button(
            canvas,
            .label      = ui_shape_scratch(g_menuChildConfig[childIndex].iconShape),
            .fontSize   = 25,
            .tooltip    = menu_child_tooltip_scratch(childIndex, isOpen, allowDetach),
            .frameColor = isOpen ? g_menuChildFrameColorOpen : g_menuChildFrameColorNormal,
            .activate   = windowActive && menu_child_hotkey_pressed(input, childIndex))) {

      if (isOpen) {
        ecs_world_entity_destroy(world, menu->childEntities[childIndex]);
        menu->childEntities[childIndex] = 0;
        menu_notify_child_state(statsGlobal, childIndex, string_lit("closed"));
      } else if (allowDetach && (input_modifiers(input) & InputModifier_Control)) {
        menu_child_open_detached(world, canvas, menu, menuEntity, childIndex);
        menu_notify_child_state(statsGlobal, childIndex, string_lit("open detached"));
      } else {
        menu_child_open(world, menu, menuEntity, childIndex);
        menu_notify_child_state(statsGlobal, childIndex, string_lit("open"));
      }
    }
  }
}

ecs_system_define(DevMenuUpdateSys) {
  EcsView*     globalView = ecs_world_view_t(world, GlobalView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (!globalItr) {
    return; // Global dependencies not initialized yet.
  }
  const InputManagerComp* input       = ecs_view_read_t(globalItr, InputManagerComp);
  DebugStatsGlobalComp*   statsGlobal = ecs_view_write_t(globalItr, DebugStatsGlobalComp);

  EcsView*     windowView = ecs_world_view_t(world, WindowView);
  EcsIterator* windowItr  = ecs_view_itr(windowView);

  EcsView* menuView = ecs_world_view_t(world, PanelUpdateView);
  for (EcsIterator* itr = ecs_view_itr(menuView); ecs_view_walk(itr);) {
    const EcsEntityId panelEntity = ecs_view_entity(itr);
    DevMenuComp*      menu        = ecs_view_write_t(itr, DevMenuComp);
    UiCanvasComp*     canvas      = ecs_view_write_t(itr, UiCanvasComp);

    ui_canvas_reset(canvas);
    if (dev_panel_hidden(ecs_view_read_t(itr, DevPanelComp))) {
      continue;
    }
    if (!ecs_view_maybe_jump(windowItr, menu->window)) {
      continue;
    }
    const GapWindowComp* win = ecs_view_read_t(windowItr, GapWindowComp);

    menu_action_bar_draw(world, panelEntity, canvas, input, menu, statsGlobal, menu->window, win);

    if (input_triggered_lit(input, "DevPanelClose")) {
      const EcsEntityId topmostChild = menu_child_topmost(world, menu);
      if (topmostChild) {
        ui_canvas_sound(canvas, UiSoundType_ClickAlt);
        ecs_world_entity_destroy(world, topmostChild);
      }
    }
  }
}

ecs_module_init(dev_menu_module) {
  ecs_register_comp(DevMenuComp);

  ecs_register_view(GlobalView);
  ecs_register_view(PanelUpdateView);
  ecs_register_view(CanvasView);
  ecs_register_view(WindowView);

  ecs_register_system(
      DevMenuUpdateSys,
      ecs_view_id(GlobalView),
      ecs_view_id(PanelUpdateView),
      ecs_view_id(CanvasView),
      ecs_view_id(WindowView));
}

EcsEntityId dev_menu_create(EcsWorld* world, const EcsEntityId window) {
  const EcsEntityId menuEntity = dev_panel_create(world, window, DevPanelType_Normal);
  DevMenuComp*      menu       = ecs_world_add_t(world, menuEntity, DevMenuComp, .window = window);

  for (u32 childIndex = 0; childIndex != array_elems(menu->childEntities); ++childIndex) {
    if (g_menuChildConfig[childIndex].autoOpen) {
      menu_child_open(world, menu, menuEntity, childIndex);
    }
  }

  return menuEntity;
}
