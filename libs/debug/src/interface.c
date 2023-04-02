#include "asset_manager.h"
#include "core_array.h"
#include "core_format.h"
#include "debug_interface.h"
#include "ecs_world.h"
#include "scene_lifetime.h"
#include "ui.h"
#include "ui_settings.h"

// clang-format off

static const String g_tooltipScale          = string_static("User interface scaling factor.\n\a.bNote\ar: Needs to be applied before taking effect.");
static const String g_tooltipDpiScaling     = string_static("Should the display's density (in 'dots per inch') be taken into account.");\
static const String g_tooltipDebugInspector = string_static("Enable the debug inspector.\n\n"
                                                            "Meaning:\n"
                                                            "- \a|01\a~red\a.bRed\ar: Element's rectangle.\n"
                                                            "- \a|01\a~blue\a.bBlue\ar: Element's container rectangle.\n");
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

ecs_comp_define(DebugInterfacePanelComp) {
  UiPanel     panel;
  EcsEntityId window;
  f32         newScale;
  i32         defaultColorIndex;
};

ecs_view_define(WindowView) { ecs_access_write(UiSettingsComp); }

ecs_view_define(PanelUpdateView) {
  ecs_access_write(DebugInterfacePanelComp);
  ecs_access_write(UiCanvasComp);
}

static void interface_panel_draw(
    UiCanvasComp* canvas, DebugInterfacePanelComp* panelComp, UiSettingsComp* settings) {

  const String title = fmt_write_scratch("{} Interface Panel", fmt_ui_shape(FormatShapes));
  ui_panel_begin(canvas, &panelComp->panel, .title = title);

  UiTable table = ui_table();
  ui_table_add_column(&table, UiTableColumn_Fixed, 150);
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
      canvas, (u32*)&settings->flags, UiSettingFlags_DpiScaling, .tooltip = g_tooltipDpiScaling);

  ui_table_next_row(canvas, &table);
  ui_label(canvas, string_lit("Default color"));
  ui_table_next_column(canvas, &table);
  ui_select(
      canvas, &panelComp->defaultColorIndex, g_defaultColorNames, array_elems(g_defaultColors));
  settings->defaultColor = g_defaultColors[panelComp->defaultColorIndex];

  ui_table_next_row(canvas, &table);
  ui_label(canvas, string_lit("Debug inspector"));
  ui_table_next_column(canvas, &table);
  ui_toggle_flag(
      canvas,
      (u32*)&settings->flags,
      UiSettingFlags_DebugInspector,
      .tooltip = g_tooltipDebugInspector);

  ui_table_next_row(canvas, &table);
  ui_label(canvas, string_lit("Debug shading"));
  ui_table_next_column(canvas, &table);
  ui_toggle_flag(
      canvas,
      (u32*)&settings->flags,
      UiSettingFlags_DebugShading,
      .tooltip = g_tooltipDebugShading);

  ui_table_next_row(canvas, &table);
  if (ui_button(canvas, .label = string_lit("Defaults"), .tooltip = g_tooltipDefaults)) {
    ui_settings_to_default(settings);
    panelComp->newScale          = settings->scale;
    panelComp->defaultColorIndex = 0;
  }
  ui_table_next_column(canvas, &table);
  if (ui_button(
          canvas,
          .label      = string_lit("Apply"),
          .frameColor = dirty ? ui_color(0, 178, 0, 192) : ui_color(32, 32, 32, 192),
          .flags      = dirty ? 0 : UiWidget_Disabled,
          .tooltip    = g_tooltipApply)) {
    settings->scale = panelComp->newScale;
  }

  ui_panel_end(canvas, &panelComp->panel);
}

ecs_system_define(DebugInterfaceUpdatePanelSys) {
  EcsIterator* windowItr = ecs_view_itr(ecs_world_view_t(world, WindowView));

  EcsView* panelView = ecs_world_view_t(world, PanelUpdateView);
  for (EcsIterator* itr = ecs_view_itr(panelView); ecs_view_walk(itr);) {
    const EcsEntityId        entity    = ecs_view_entity(itr);
    DebugInterfacePanelComp* panelComp = ecs_view_write_t(itr, DebugInterfacePanelComp);
    UiCanvasComp*            canvas    = ecs_view_write_t(itr, UiCanvasComp);

    if (!ecs_view_maybe_jump(windowItr, panelComp->window)) {
      continue; // Window has been destroyed, or has no ui settings.
    }
    UiSettingsComp* settings = ecs_view_write_t(windowItr, UiSettingsComp);

    if (panelComp->newScale == 0) {
      panelComp->newScale = settings->scale;
    }

    ui_canvas_reset(canvas);
    interface_panel_draw(canvas, panelComp, settings);

    if (panelComp->panel.flags & UiPanelFlags_Close) {
      ecs_world_entity_destroy(world, entity);
    }
    if (ui_canvas_status(canvas) >= UiStatus_Pressed) {
      ui_canvas_to_front(canvas);
    }
  }
}

ecs_module_init(debug_interface_module) {
  ecs_register_comp(DebugInterfacePanelComp);

  ecs_register_view(WindowView);
  ecs_register_view(PanelUpdateView);

  ecs_register_system(
      DebugInterfaceUpdatePanelSys, ecs_view_id(PanelUpdateView), ecs_view_id(WindowView));
}

EcsEntityId debug_interface_panel_open(EcsWorld* world, const EcsEntityId window) {
  const EcsEntityId panelEntity = ui_canvas_create(world, window, UiCanvasCreateFlags_ToFront);
  ecs_world_add_t(
      world,
      panelEntity,
      DebugInterfacePanelComp,
      .panel  = ui_panel(.position = ui_vector(0.75f, 0.5f), .size = ui_vector(330, 190)),
      .window = window);
  return panelEntity;
}
