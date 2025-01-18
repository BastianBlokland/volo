#include "core_array.h"
#include "core_format.h"
#include "dev_interface.h"
#include "dev_panel.h"
#include "ecs_view.h"
#include "ecs_world.h"
#include "ui_canvas.h"
#include "ui_layout.h"
#include "ui_panel.h"
#include "ui_settings.h"
#include "ui_shape.h"
#include "ui_table.h"
#include "ui_widget.h"

// clang-format off

static const String g_tooltipScale          = string_static("User interface scaling factor.\n\a.bNote\ar: Needs to be applied before taking effect.");
static const String g_tooltipDpiScaling     = string_static("Should the display's density (in 'dots per inch') be taken into account.");\
static const String g_tooltipDebugInspector = string_static("Enable the debug inspector.\n\n"
                                                            "Meaning:\n"
                                                            "- \a|01\a~red\a.bRed\ar: Element's rectangle.\n"
                                                            "- \a|01\a~green\a.bGreen\ar: Element's container's logic rectangle.\n"
                                                            "- \a|01\a~blue\a.bBlue\ar: Element's container's clip rectangle.\n");
static const String g_tooltipDebugShading   = string_static("Enable the debug shading.\n\n"
                                                            "Meaning:\n"
                                                            "- \a#001CFFFF\a|01\a.bBlue\ar: Dark is fully inside the shape and light is on the shape's outer edge.\n"
                                                            "- \a#FFFFFFFF\a|01White\ar: The shape's outline.\n"
                                                            "- \a#00FF00FF\a|01\a.bGreen\ar: Dark is on the shape's outer edge and light is fully outside the shape.\n");
static const String g_tooltipApply          = string_static("Apply outstanding interface setting changes.");
static const String g_tooltipDefaults       = string_static("Reset all settings to their defaults.");

// clang-format on

static const UiColor g_defaultColors[] = {
    {255, 255, 255, 255},
    {32, 255, 32, 255},
    {255, 255, 32, 255},
    {32, 255, 255, 255},
    {232, 232, 232, 192},
};
static const String g_defaultColorNames[] = {
    string_static("\a#FFFFFFFFWhite"),
    string_static("\a#32FF32FFGreen"),
    string_static("\a#FFFF32FFYellow"),
    string_static("\a#32FFFFFFAqua"),
    string_static("\a#E8E8E8C0Silver"),
};
ASSERT(array_elems(g_defaultColors) == array_elems(g_defaultColorNames), "Missing names");

static const String g_inspectorModeNames[] = {
    string_static("None"),
    string_static("DebugInteractables"),
    string_static("DebugAll"),
};
ASSERT(array_elems(g_inspectorModeNames) == UiInspectorMode_Count, "Missing inspector names");

ecs_comp_define(DevInterfacePanelComp) {
  UiPanel     panel;
  EcsEntityId window;
  f32         newScale;
  i32         defaultColorIndex;
};

ecs_view_define(GlobalView) { ecs_access_write(UiSettingsGlobalComp); }

ecs_view_define(PanelUpdateView) {
  ecs_view_flags(EcsViewFlags_Exclusive); // DevInterfacePanelComp's are exclusively managed here.

  ecs_access_read(DevPanelComp);
  ecs_access_write(DevInterfacePanelComp);
  ecs_access_write(UiCanvasComp);
}

static void interface_panel_draw(
    UiCanvasComp* canvas, DevInterfacePanelComp* panelComp, UiSettingsGlobalComp* settings) {

  const String title = fmt_write_scratch("{} Interface Panel", fmt_ui_shape(FormatShapes));
  ui_panel_begin(
      canvas, &panelComp->panel, .title = title, .topBarColor = ui_color(100, 0, 0, 192));

  UiTable table = ui_table();
  ui_table_add_column(&table, UiTableColumn_Fixed, 200);
  ui_table_add_column(&table, UiTableColumn_Flexible, 0);

  bool dirty = false;
  dirty |= panelComp->newScale != settings->scale;

  ui_table_next_row(canvas, &table);
  ui_label(canvas, string_lit("Scale factor"));
  ui_table_next_column(canvas, &table);
  ui_slider(canvas, &panelComp->newScale, .min = 0.5, .max = 2, .tooltip = g_tooltipScale);

  ui_table_next_row(canvas, &table);
  ui_label(canvas, string_lit("Dpi scaling"));
  ui_table_next_column(canvas, &table);
  ui_toggle_flag(
      canvas, (u32*)&settings->flags, UiSettingGlobal_DpiScaling, .tooltip = g_tooltipDpiScaling);

  ui_table_next_row(canvas, &table);
  ui_label(canvas, string_lit("Default color"));
  ui_table_next_column(canvas, &table);
  ui_select(
      canvas, &panelComp->defaultColorIndex, g_defaultColorNames, array_elems(g_defaultColors));
  settings->defaultColor = g_defaultColors[panelComp->defaultColorIndex];

  ui_table_next_row(canvas, &table);
  ui_label(canvas, string_lit("Debug inspector"));
  ui_table_next_column(canvas, &table);
  ui_select(
      canvas,
      (i32*)&settings->inspectorMode,
      g_inspectorModeNames,
      array_elems(g_inspectorModeNames),
      .tooltip = g_tooltipDebugInspector);

  ui_table_next_row(canvas, &table);
  ui_label(canvas, string_lit("Debug shading"));
  ui_table_next_column(canvas, &table);
  ui_toggle_flag(
      canvas,
      (u32*)&settings->flags,
      UiSettingGlobal_DebugShading,
      .tooltip = g_tooltipDebugShading);

  ui_table_next_row(canvas, &table);
  if (ui_button(canvas, .label = string_lit("Defaults"), .tooltip = g_tooltipDefaults)) {
    ui_settings_global_to_default(settings);
    panelComp->newScale          = settings->scale;
    panelComp->defaultColorIndex = 0;
  }
  ui_table_next_column(canvas, &table);
  ui_layout_push(canvas);
  ui_layout_resize(canvas, UiAlign_BottomLeft, ui_vector(200, 0), UiBase_Absolute, Ui_X);
  if (ui_button(
          canvas,
          .label      = string_lit("Apply"),
          .frameColor = dirty ? ui_color(0, 178, 0, 192) : ui_color(32, 32, 32, 192),
          .flags      = dirty ? 0 : UiWidget_Disabled,
          .tooltip    = g_tooltipApply)) {
    settings->scale = panelComp->newScale;
  }
  ui_layout_pop(canvas);

  ui_panel_end(canvas, &panelComp->panel);
}

ecs_system_define(DebugInterfaceUpdatePanelSys) {
  EcsView*     globalView = ecs_world_view_t(world, GlobalView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (!globalItr) {
    return; // Global dependencies not initialized yet.
  }
  UiSettingsGlobalComp* settings = ecs_view_write_t(globalItr, UiSettingsGlobalComp);

  EcsView* panelView = ecs_world_view_t(world, PanelUpdateView);
  for (EcsIterator* itr = ecs_view_itr(panelView); ecs_view_walk(itr);) {
    const EcsEntityId      entity    = ecs_view_entity(itr);
    DevInterfacePanelComp* panelComp = ecs_view_write_t(itr, DevInterfacePanelComp);
    UiCanvasComp*          canvas    = ecs_view_write_t(itr, UiCanvasComp);

    if (panelComp->newScale == 0) {
      panelComp->newScale = settings->scale;
    }

    ui_canvas_reset(canvas);
    const bool pinned = ui_panel_pinned(&panelComp->panel);
    if (dev_panel_hidden(ecs_view_read_t(itr, DevPanelComp)) && !pinned) {
      settings->flags &= ~UiSettingGlobal_DebugShading;
      settings->inspectorMode = UiInspectorMode_None;
      continue;
    }
    interface_panel_draw(canvas, panelComp, settings);

    if (ui_panel_closed(&panelComp->panel)) {
      ecs_world_entity_destroy(world, entity);
    }
    if (ui_canvas_status(canvas) >= UiStatus_Pressed) {
      ui_canvas_to_front(canvas);
    }
  }
}

ecs_module_init(debug_interface_module) {
  ecs_register_comp(DevInterfacePanelComp);

  ecs_register_view(GlobalView);
  ecs_register_view(PanelUpdateView);

  ecs_register_system(
      DebugInterfaceUpdatePanelSys, ecs_view_id(GlobalView), ecs_view_id(PanelUpdateView));
}

EcsEntityId
dev_interface_panel_open(EcsWorld* world, const EcsEntityId window, const DevPanelType type) {
  const EcsEntityId      panelEntity    = dev_panel_create(world, window, type);
  DevInterfacePanelComp* interfacePanel = ecs_world_add_t(
      world,
      panelEntity,
      DevInterfacePanelComp,
      .panel  = ui_panel(.position = ui_vector(0.5f, 0.5f), .size = ui_vector(500, 190)),
      .window = window);

  if (type == DevPanelType_Detached) {
    ui_panel_maximize(&interfacePanel->panel);
  }

  return panelEntity;
}
